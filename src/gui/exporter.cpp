// Exportação: fotos convertidas e sequências (vídeo / imagem animada).
// Tudo aqui é igual no Windows e no Mac; o que depende do sistema (vídeo H.264
// e HEIC) fica em platform/media.h.

#ifdef _WIN32
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "exporter.h"
#include "photo_store.h"
#include "platform/media.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <thread>

#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include <webp/encode.h>
#include <webp/mux.h>
#include <avif/avif.h>

#define MINIMP4_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "minimp4.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace fs = std::filesystem;

namespace
{
    bool WriteBytes(const fs::path& file, const void* data, size_t size)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return static_cast<bool>(out);
    }

    void AppendToVector(void* context, void* data, int size)
    {
        auto* out = static_cast<std::vector<unsigned char>*>(context);
        out->insert(out->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
    }

    // Reduz a foto para caber em w x h, centralizada em fundo preto
    std::vector<unsigned char> Fit(const RgbFrame& src, int w, int h)
    {
        std::vector<unsigned char> out(static_cast<size_t>(w) * h * 3, 0);
        const double scale = std::min(static_cast<double>(w) / src.width, static_cast<double>(h) / src.height);
        const int fw = std::max(1, std::min(w, static_cast<int>(std::lround(src.width * scale))));
        const int fh = std::max(1, std::min(h, static_cast<int>(std::lround(src.height * scale))));
        std::vector<unsigned char> resized(static_cast<size_t>(fw) * fh * 3);
        stbir_resize_uint8_srgb(src.pixels.data(), src.width, src.height, 0, resized.data(), fw, fh, 0, STBIR_RGB);
        const int ox = (w - fw) / 2, oy = (h - fh) / 2;
        for (int y = 0; y < fh; ++y)
            std::memcpy(&out[(static_cast<size_t>(y + oy) * w + ox) * 3], &resized[static_cast<size_t>(y) * fw * 3],
                        static_cast<size_t>(fw) * 3);
        return out;
    }

    // ------------------------------------------------------------------
    // GIF: uma paleta de 256 cores para todos os quadros (sem "piscar" de cor
    // entre quadros), pontilhado ordenado e compressão LZW. Repete sem parar.
    // ------------------------------------------------------------------
    struct Box { int lo[3], hi[3]; std::vector<int> bins; uint64_t count = 0; };

    std::vector<std::array<int, 3>> MedianCut(const std::vector<uint32_t>& histogram)
    {
        // Cores com 5 bits por canal (32768 "caixinhas") como ponto de partida
        auto channel = [](int bin, int c) { return (bin >> (10 - 5 * c)) & 31; };
        std::vector<Box> boxes(1);
        for (int bin = 0; bin < 32768; ++bin)
            if (histogram[bin])
            {
                boxes[0].bins.push_back(bin);
                boxes[0].count += histogram[bin];
            }
        auto shrink = [&](Box& b)
        {
            for (int c = 0; c < 3; ++c) { b.lo[c] = 31; b.hi[c] = 0; }
            for (int bin : b.bins)
                for (int c = 0; c < 3; ++c)
                {
                    b.lo[c] = std::min(b.lo[c], channel(bin, c));
                    b.hi[c] = std::max(b.hi[c], channel(bin, c));
                }
        };
        if (boxes[0].bins.empty())
            return { { 0, 0, 0 } };
        shrink(boxes[0]);
        while (boxes.size() < 256)
        {
            // Divide a caixa com mais pixels vezes o maior lado
            int best = -1;
            double bestScore = 0;
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                const auto& b = boxes[i];
                if (b.bins.size() < 2)
                    continue;
                const int range = std::max({ b.hi[0] - b.lo[0], b.hi[1] - b.lo[1], b.hi[2] - b.lo[2] });
                const double score = static_cast<double>(range) * std::sqrt(static_cast<double>(b.count));
                if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
            }
            if (best < 0)
                break;
            Box& b = boxes[best];
            int c = 0;
            for (int k = 1; k < 3; ++k)
                if (b.hi[k] - b.lo[k] > b.hi[c] - b.lo[c]) c = k;
            std::sort(b.bins.begin(), b.bins.end(), [&](int x, int y) { return channel(x, c) < channel(y, c); });
            uint64_t half = b.count / 2, acc = 0;
            size_t cut = 1;
            for (; cut < b.bins.size(); ++cut)
            {
                acc += histogram[b.bins[cut - 1]];
                if (acc >= half) break;
            }
            cut = std::clamp<size_t>(cut, 1, b.bins.size() - 1);
            Box other;
            other.bins.assign(b.bins.begin() + static_cast<long>(cut), b.bins.end());
            b.bins.resize(cut);
            b.count = 0;
            for (int bin : b.bins) b.count += histogram[bin];
            for (int bin : other.bins) other.count += histogram[bin];
            shrink(b);
            shrink(other);
            boxes.push_back(std::move(other));
        }
        std::vector<std::array<int, 3>> palette;
        for (const auto& b : boxes)
        {
            double sum[3] = { 0, 0, 0 };
            for (int bin : b.bins)
                for (int c = 0; c < 3; ++c)
                    sum[c] += (channel(bin, c) * 8 + 4) * static_cast<double>(histogram[bin]);
            std::array<int, 3> color{};
            for (int c = 0; c < 3; ++c)
                color[c] = std::clamp(static_cast<int>(std::lround(sum[c] / std::max<uint64_t>(1, b.count))), 0, 255);
            palette.push_back(color);
        }
        return palette;
    }

    class GifWriter
    {
    public:
        explicit GifWriter(std::vector<unsigned char>& out) : out(out) {}

        void Lzw(const std::vector<unsigned char>& indices)
        {
            const int minCode = 8, clear = 256, eoi = 257;
            static std::vector<uint16_t> codes(1 << 20);
            static std::vector<uint32_t> stamps(1 << 20, 0);
            static uint32_t generation = 0;
            ++generation;
            int codeSize = minCode + 1, next = eoi + 1;
            out.push_back(static_cast<unsigned char>(minCode));
            bits = 0; bitCount = 0; block.clear();
            Emit(clear, codeSize);
            int prefix = indices.empty() ? 0 : indices[0];
            for (size_t i = 1; i < indices.size(); ++i)
            {
                const int c = indices[i];
                const uint32_t key = (static_cast<uint32_t>(prefix) << 8) | static_cast<uint32_t>(c);
                if (stamps[key] == generation)
                {
                    prefix = codes[key];
                    continue;
                }
                Emit(prefix, codeSize);
                codes[key] = static_cast<uint16_t>(next);
                stamps[key] = generation;
                ++next;
                if (next > (1 << codeSize) && codeSize < 12)
                    ++codeSize;
                if (next >= 4096)
                {
                    Emit(clear, codeSize);
                    ++generation;
                    codeSize = minCode + 1;
                    next = eoi + 1;
                }
                prefix = c;
            }
            Emit(prefix, codeSize);
            Emit(eoi, codeSize);
            if (bitCount > 0)
                Byte(static_cast<unsigned char>(bits & 0xFF));
            FlushBlock();
            out.push_back(0);  // fim dos blocos
        }

    private:
        void Emit(int code, int size)
        {
            bits |= static_cast<uint32_t>(code) << bitCount;
            bitCount += size;
            while (bitCount >= 8)
            {
                Byte(static_cast<unsigned char>(bits & 0xFF));
                bits >>= 8;
                bitCount -= 8;
            }
        }
        void Byte(unsigned char b)
        {
            block.push_back(b);
            if (block.size() == 255)
                FlushBlock();
        }
        void FlushBlock()
        {
            if (block.empty())
                return;
            out.push_back(static_cast<unsigned char>(block.size()));
            out.insert(out.end(), block.begin(), block.end());
            block.clear();
        }

        std::vector<unsigned char>& out;
        std::vector<unsigned char> block;
        uint32_t bits = 0;
        int bitCount = 0;
    };

    bool WriteGif(const std::vector<std::vector<unsigned char>>& frames, int w, int h, int fps, const fs::path& file)
    {
        std::vector<uint32_t> histogram(32768, 0);
        for (const auto& f : frames)
            for (size_t i = 0; i + 2 < f.size(); i += 3)
                ++histogram[((f[i] >> 3) << 10) | ((f[i + 1] >> 3) << 5) | (f[i + 2] >> 3)];
        auto palette = MedianCut(histogram);
        palette.resize(256, { 0, 0, 0 });

        // Cor mais próxima para cada uma das 32768 caixinhas
        std::vector<unsigned char> lookup(32768);
        for (int bin = 0; bin < 32768; ++bin)
        {
            const int r = ((bin >> 10) & 31) * 8 + 4, g = ((bin >> 5) & 31) * 8 + 4, b = (bin & 31) * 8 + 4;
            int best = 0, bestDist = 1 << 30;
            for (int p = 0; p < 256; ++p)
            {
                const int dr = r - palette[p][0], dg = g - palette[p][1], db = b - palette[p][2];
                const int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
                if (d < bestDist) { bestDist = d; best = p; }
            }
            lookup[bin] = static_cast<unsigned char>(best);
        }

        std::vector<unsigned char> out;
        auto u16 = [&](int v) { out.push_back(static_cast<unsigned char>(v & 0xFF)); out.push_back(static_cast<unsigned char>(v >> 8)); };
        const char* header = "GIF89a";
        out.insert(out.end(), header, header + 6);
        u16(w); u16(h);
        out.push_back(0xF7);  // paleta global de 256 cores
        out.push_back(0); out.push_back(0);
        for (const auto& c : palette)
            for (int k = 0; k < 3; ++k) out.push_back(static_cast<unsigned char>(c[k]));
        // Repetir sem parar
        const unsigned char loop[] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00 };
        out.insert(out.end(), loop, loop + sizeof(loop));

        static const int bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        GifWriter writer(out);
        double clock = 0;
        int written = 0;
        std::vector<unsigned char> indices(static_cast<size_t>(w) * h);
        for (const auto& f : frames)
        {
            clock += 100.0 / fps;
            const int delay = static_cast<int>(std::lround(clock)) - written;  // centésimos de segundo
            written += delay;
            const unsigned char gce[] = { 0x21, 0xF9, 0x04, 0x04, static_cast<unsigned char>(delay & 0xFF),
                                          static_cast<unsigned char>(delay >> 8), 0x00, 0x00 };
            out.insert(out.end(), gce, gce + sizeof(gce));
            out.push_back(0x2C);
            u16(0); u16(0); u16(w); u16(h);
            out.push_back(0);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = static_cast<size_t>(y) * w + x;
                    const int d = (bayer[y & 3][x & 3] - 8);  // -8..7: meio "degrau" de cor
                    const int r = std::clamp(f[i * 3] + d, 0, 255), g = std::clamp(f[i * 3 + 1] + d, 0, 255),
                              b = std::clamp(f[i * 3 + 2] + d, 0, 255);
                    indices[i] = lookup[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
                }
            writer.Lzw(indices);
        }
        out.push_back(0x3B);
        return WriteBytes(file, out.data(), out.size());
    }

    bool WriteWebpAnimation(const std::vector<std::vector<unsigned char>>& frames, int w, int h, int fps,
                            const fs::path& file)
    {
        WebPAnimEncoderOptions options;
        if (!WebPAnimEncoderOptionsInit(&options))
            return false;
        options.anim_params.loop_count = 0;  // sem parar
        WebPAnimEncoder* encoder = WebPAnimEncoderNew(w, h, &options);
        if (!encoder)
            return false;
        WebPConfig config;
        WebPConfigInit(&config);
        config.quality = 85;
        config.method = 4;
        bool ok = true;
        int timestamp = 0;
        for (size_t i = 0; i < frames.size() && ok; ++i)
        {
            WebPPicture picture;
            WebPPictureInit(&picture);
            picture.width = w;
            picture.height = h;
            picture.use_argb = 1;
            ok = WebPPictureImportRGB(&picture, frames[i].data(), w * 3)
                 && WebPAnimEncoderAdd(encoder, &picture, timestamp, &config);
            WebPPictureFree(&picture);
            timestamp = static_cast<int>(std::lround((i + 1) * 1000.0 / fps));
        }
        WebPData data;
        WebPDataInit(&data);
        ok = ok && WebPAnimEncoderAdd(encoder, nullptr, timestamp, nullptr) && WebPAnimEncoderAssemble(encoder, &data)
             && WriteBytes(file, data.bytes, data.size);
        WebPDataClear(&data);
        WebPAnimEncoderDelete(encoder);
        return ok;
    }

    bool WriteAvif(const std::vector<std::vector<unsigned char>>& frames, int w, int h, int fps, const fs::path& file,
                   const Export::Progress& progress, const std::atomic<bool>* cancel)
    {
        avifEncoder* encoder = avifEncoderCreate();
        if (!encoder)
            return false;
        encoder->speed = 8;
        encoder->quality = 60;
        encoder->maxThreads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        encoder->timescale = static_cast<uint64_t>(fps);
        encoder->repetitionCount = AVIF_REPETITION_COUNT_INFINITE;
        bool ok = true;
        for (size_t i = 0; i < frames.size() && ok; ++i)
        {
            if (cancel && *cancel)
                ok = false;
            avifImage* image = avifImageCreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h), 8, AVIF_PIXEL_FORMAT_YUV420);
            avifRGBImage rgb;
            avifRGBImageSetDefaults(&rgb, image);
            rgb.format = AVIF_RGB_FORMAT_RGB;
            rgb.pixels = const_cast<uint8_t*>(frames[i].data());
            rgb.rowBytes = static_cast<uint32_t>(w) * 3;
            ok = ok && avifImageRGBToYUV(image, &rgb) == AVIF_RESULT_OK
                 && avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_NONE) == AVIF_RESULT_OK;
            avifImageDestroy(image);
            if (progress)
                progress(static_cast<double>(i + 1) / frames.size(), "AVIF");
        }
        avifRWData output = AVIF_DATA_EMPTY;
        ok = ok && avifEncoderFinish(encoder, &output) == AVIF_RESULT_OK && WriteBytes(file, output.data, output.size);
        avifRWDataFree(&output);
        avifEncoderDestroy(encoder);
        return ok;
    }

    // MP4 / MOV: H.264 do sistema, guardado no arquivo pelo minimp4
    int MuxWrite(int64_t offset, const void* buffer, size_t size, void* token)
    {
        auto* out = static_cast<std::fstream*>(token);
        out->seekp(offset);
        out->write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
        return out->good() ? 0 : 1;
    }

    bool WriteVideo(const std::vector<std::vector<unsigned char>>& cycle, const std::vector<int>& order, int w, int h,
                    int fps, const fs::path& file, bool mov, const Export::Progress& progress,
                    const std::atomic<bool>* cancel, std::string& error)
    {
        auto encoder = Platform::CreateH264Encoder(w, h, fps, error);
        if (!encoder)
            return false;
        std::fstream out(file, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!out)
        {
            error = "Could not create " + file.filename().string();
            return false;
        }
        MP4E_mux_t* mux = MP4E_open(0, 0, &out, MuxWrite);
        mp4_h26x_writer_t writer;
        if (!mux || mp4_h26x_write_init(&writer, mux, w, h, 0) != MP4E_STATUS_OK)
        {
            error = "Could not start the video file.";
            return false;
        }
        const unsigned duration = static_cast<unsigned>(90000 / fps);
        bool ok = true;
        std::vector<unsigned char> nal;
        auto flush = [&]
        {
            if (!nal.empty() && mp4_h26x_write_nal(&writer, nal.data(), static_cast<int>(nal.size()), duration) != MP4E_STATUS_OK)
            {
                error = "Could not write the video.";
                ok = false;
            }
            nal.clear();
        };
        for (size_t i = 0; i < order.size() && ok; ++i)
        {
            if (cancel && *cancel)
            {
                error = "cancelled";
                ok = false;
                break;
            }
            ok = encoder->Encode(cycle[order[i]].data(), nal, error);
            flush();
            if (progress)
                progress(static_cast<double>(i + 1) / order.size(), mov ? "MOV" : "MP4");
        }
        if (ok)
        {
            ok = encoder->Finish(nal, error);
            flush();
        }
        mp4_h26x_write_close(&writer);
        MP4E_close(mux);
        out.close();
        if (ok && mov)
        {
            // MOV: mesma estrutura, marcada como QuickTime ("qt  ")
            std::fstream patch(file, std::ios::binary | std::ios::in | std::ios::out);
            char head[64] = {};
            patch.read(head, sizeof(head));
            if (std::memcmp(head + 4, "ftyp", 4) == 0)
            {
                const uint32_t size = (static_cast<unsigned char>(head[0]) << 24) | (static_cast<unsigned char>(head[1]) << 16)
                                      | (static_cast<unsigned char>(head[2]) << 8) | static_cast<unsigned char>(head[3]);
                patch.clear();
                patch.seekp(8);
                patch.write("qt  ", 4);
                for (uint32_t pos = 16; pos + 4 <= size && pos + 4 <= sizeof(head); pos += 4)
                {
                    patch.seekp(pos);
                    patch.write("qt  ", 4);
                }
            }
        }
        if (!ok)
        {
            std::error_code ec;
            fs::remove(file, ec);
        }
        return ok;
    }

    // TIFF simples (sem compressão), como a maioria dos programas de foto grava
    bool WriteTiff(const RgbFrame& image, const fs::path& file)
    {
        std::vector<unsigned char> out;
        auto u16 = [&](uint32_t v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
        auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) out.push_back((v >> (8 * k)) & 0xFF); };
        const uint32_t dataSize = static_cast<uint32_t>(image.pixels.size());
        const int entries = 10;
        const uint32_t ifd = 8, bpsOffset = ifd + 2 + entries * 12 + 4, dataOffset = bpsOffset + 6;
        out.insert(out.end(), { 'I', 'I', 42, 0 });
        u32(ifd);
        u16(entries);
        auto entry = [&](uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
        {
            u16(tag); u16(type); u32(count);
            if (type == 3 && count == 1) { u16(value); u16(0); }
            else u32(value);
        };
        entry(256, 4, 1, static_cast<uint32_t>(image.width));
        entry(257, 4, 1, static_cast<uint32_t>(image.height));
        entry(258, 3, 3, bpsOffset);  // 8, 8, 8
        entry(259, 3, 1, 1);          // sem compressão
        entry(262, 3, 1, 2);          // RGB
        entry(273, 4, 1, dataOffset);
        entry(277, 3, 1, 3);
        entry(278, 4, 1, static_cast<uint32_t>(image.height));
        entry(279, 4, 1, dataSize);
        entry(284, 3, 1, 1);
        u32(0);
        u16(8); u16(8); u16(8);
        out.insert(out.end(), image.pixels.begin(), image.pixels.end());
        return WriteBytes(file, out.data(), out.size());
    }
}

namespace Export
{
    std::vector<int> CycleOrder(int photos, bool reverse)
    {
        std::vector<int> order;
        for (int i = 0; i < photos; ++i)
            order.push_back(i);
        if (reverse)
            for (int i = photos - 2; i >= 1; --i)
                order.push_back(i);
        return order;
    }

    bool FormatAvailable(const std::string& format, std::string& reason)
    {
        if (format == "heic")
            return Platform::HeicAvailable(reason);
        return true;
    }

    bool WriteSequence(const std::vector<fs::path>& photos, const fs::path& base, const SequenceOptions& options,
                       const Progress& progress, const std::atomic<bool>* cancel, std::string& error)
    {
        if (photos.empty())
        {
            error = "This session has no photos.";
            return false;
        }
        const int fps = std::clamp(static_cast<int>(std::lround(options.fps)), 1, 60);

        // Fotos decodificadas uma vez; o tamanho segue a primeira
        std::vector<RgbFrame> decoded;
        for (size_t i = 0; i < photos.size(); ++i)
        {
            if (cancel && *cancel) { error = "cancelled"; return false; }
            RgbFrame frame;
            if (!LoadPhotoRgb(photos[i], frame))
            {
                error = "Could not read " + photos[i].filename().string();
                return false;
            }
            decoded.push_back(std::move(frame));
            if (progress)
                progress(static_cast<double>(i + 1) / photos.size(), "photos");
        }
        auto sizeFor = [&](int height, int& w, int& h)
        {
            const double aspect = static_cast<double>(decoded[0].width) / decoded[0].height;
            const int boxW = height * 16 / 9, boxH = height;
            if (aspect >= static_cast<double>(boxW) / boxH) { w = boxW; h = static_cast<int>(std::lround(boxW / aspect)); }
            else { h = boxH; w = static_cast<int>(std::lround(boxH * aspect)); }
            w = std::max(16, w & ~15);  // largura múltipla de 16: aceita por todos os codificadores de vídeo
            h = std::max(2, h & ~1);
        };

        const std::vector<int> cycle = CycleOrder(static_cast<int>(decoded.size()), options.reverse);
        std::vector<int> video;
        for (int l = 0; l < std::max(1, options.loops); ++l)
            video.insert(video.end(), cycle.begin(), cycle.end());

        int cachedHeight = 0, w = 0, h = 0;
        std::vector<std::vector<unsigned char>> fitted;  // uma imagem por foto, no tamanho atual
        auto prepare = [&](int height)
        {
            if (height == cachedHeight)
                return;
            sizeFor(height, w, h);
            fitted.clear();
            for (const auto& d : decoded)
                fitted.push_back(Fit(d, w, h));
            cachedHeight = height;
        };
        auto inCycle = [&]
        {
            std::vector<std::vector<unsigned char>> frames;
            for (int i : cycle)
                frames.push_back(fitted[i]);
            return frames;
        };

        for (const std::string& format : options.formats)
        {
            if (cancel && *cancel) { error = "cancelled"; return false; }
            const bool animated = format == "gif" || format == "webp" || format == "avif";
            prepare(animated ? std::min(options.height, 1080) : options.height);
            const fs::path file = fs::path(base).concat("." + format);
            if (progress)
                progress(0, [&] { std::string up = format; for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return up; }());
            bool ok = false;
            if (format == "mp4" || format == "mov")
                ok = WriteVideo(fitted, video, w, h, fps, file, format == "mov", progress, cancel, error);
            else if (format == "gif")
                ok = WriteGif(inCycle(), w, h, fps, file);
            else if (format == "webp")
                ok = WriteWebpAnimation(inCycle(), w, h, fps, file);
            else if (format == "avif")
                ok = WriteAvif(inCycle(), w, h, fps, file, progress, cancel);
            if (!ok)
            {
                if (error.empty())
                    error = "Could not create " + file.filename().string();
                return false;
            }
        }
        return true;
    }

    bool ConvertPhoto(const fs::path& photo, const fs::path& dest, const std::string& format, std::string& error)
    {
        const fs::path file = fs::path(dest).concat("." + format);
        std::string jpeg;
        RgbFrame image;
        // De JPEG para JPG: o próprio arquivo, sem perder qualidade nem os dados da câmera
        if (format == "jpg" && IsJpegPhoto(photo.filename().string()))
        {
            std::error_code ec;
            fs::copy_file(photo, file, fs::copy_options::overwrite_existing, ec);
            if (ec) error = "Could not save " + file.filename().string();
            return !ec;
        }
        if (!LoadPhotoRgb(photo, image, &jpeg))
        {
            error = "Could not read " + photo.filename().string();
            return false;
        }
        bool ok = false;
        if (format == "jpg")
        {
            std::vector<unsigned char> out;
            ok = stbi_write_jpg_to_func(AppendToVector, &out, image.width, image.height, 3, image.pixels.data(), 95)
                 && WriteBytes(file, out.data(), out.size());
        }
        else if (format == "png")
        {
            std::vector<unsigned char> out;
            ok = stbi_write_png_to_func(AppendToVector, &out, image.width, image.height, 3, image.pixels.data(),
                                        image.width * 3)
                 && WriteBytes(file, out.data(), out.size());
        }
        else if (format == "webp")
        {
            uint8_t* out = nullptr;
            const size_t size = WebPEncodeRGB(image.pixels.data(), image.width, image.height, image.width * 3, 92, &out);
            ok = size > 0 && WriteBytes(file, out, size);
            WebPFree(out);
        }
        else if (format == "tiff")
            ok = WriteTiff(image, file);
        else if (format == "heic")
            ok = Platform::WriteHeic(image.pixels.data(), image.width, image.height, file, error);
        if (!ok && error.empty())
            error = "Could not save " + file.filename().string();
        return ok;
    }
}
