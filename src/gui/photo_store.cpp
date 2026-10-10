#include "photo_store.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace
{
    const char* const kPreviewFolder = ".previews";

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    bool IsJpeg(const std::string& file)
    {
        std::string ext = fs::path(file).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        return ext == ".jpg" || ext == ".jpeg";
    }

    // Orientação EXIF (1 = normal, 3 = 180°, 6 = 90° horário, 8 = 90° anti-horário)
    int ReadExifOrientation(const std::string& jpeg)
    {
        size_t pos = 2;
        while (pos + 4 < jpeg.size() && static_cast<unsigned char>(jpeg[pos]) == 0xFF)
        {
            const unsigned char marker = static_cast<unsigned char>(jpeg[pos + 1]);
            const size_t length = (static_cast<unsigned char>(jpeg[pos + 2]) << 8) | static_cast<unsigned char>(jpeg[pos + 3]);
            if (marker == 0xE1 && jpeg.compare(pos + 4, 6, std::string("Exif\0\0", 6)) == 0)
            {
                const size_t tiff = pos + 10;
                if (tiff + 8 > jpeg.size())
                    return 1;
                const bool little = jpeg[tiff] == 'I';
                auto u16 = [&](size_t at) -> unsigned
                {
                    const unsigned a = static_cast<unsigned char>(jpeg[at]), b = static_cast<unsigned char>(jpeg[at + 1]);
                    return little ? (a | (b << 8)) : ((a << 8) | b);
                };
                auto u32 = [&](size_t at) -> unsigned
                {
                    return little ? (u16(at) | (u16(at + 2) << 16)) : ((u16(at) << 16) | u16(at + 2));
                };
                const size_t ifd = tiff + u32(tiff + 4);
                if (ifd + 2 > jpeg.size())
                    return 1;
                const unsigned entries = u16(ifd);
                for (unsigned i = 0; i < entries; ++i)
                {
                    const size_t entry = ifd + 2 + i * 12;
                    if (entry + 12 > jpeg.size())
                        break;
                    if (u16(entry) == 0x0112)
                        return static_cast<int>(u16(entry + 8));
                }
                return 1;
            }
            if (marker == 0xDA)
                break;
            pos += 2 + length;
        }
        return 1;
    }

    std::vector<unsigned char> Rotate(const std::vector<unsigned char>& src, int& w, int& h, int orientation)
    {
        if (orientation != 3 && orientation != 6 && orientation != 8)
            return src;

        const int nw = (orientation == 3) ? w : h;
        const int nh = (orientation == 3) ? h : w;
        std::vector<unsigned char> dst(static_cast<size_t>(nw) * nh * 3);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                int nx = x, ny = y;
                if (orientation == 3) { nx = w - 1 - x; ny = h - 1 - y; }
                if (orientation == 6) { nx = h - 1 - y; ny = x; }
                if (orientation == 8) { nx = y; ny = w - 1 - x; }
                const size_t s = (static_cast<size_t>(y) * w + x) * 3;
                const size_t d = (static_cast<size_t>(ny) * nw + nx) * 3;
                dst[d] = src[s];
                dst[d + 1] = src[s + 1];
                dst[d + 2] = src[s + 2];
            }
        }
        w = nw;
        h = nh;
        return dst;
    }

    void AppendToString(void* context, void* data, int size)
    {
        static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<size_t>(size));
    }
}

PhotoStore::PhotoStore(fs::path root) : root(std::move(root))
{
    std::error_code ec;
    fs::create_directories(this->root, ec);
}

fs::path PhotoStore::SessionPath(const std::string& session) const
{
    // Só aceita um nome de pasta simples (sem "..", barras etc.)
    if (session.empty() || session.find_first_of("/\\") != std::string::npos || session == "." || session == "..")
        return {};
    return root / session;
}

std::vector<StoredSession> PhotoStore::ListSessions() const
{
    std::vector<StoredSession> sessions;
    std::error_code ec;
    if (!fs::exists(root, ec))
        return sessions;

    static const std::regex photoName(R"(^(\d+)_([^_]+_\d+)_.+$)");

    for (const auto& dir : fs::directory_iterator(root, ec))
    {
        if (!dir.is_directory() || dir.path().filename().string().rfind('.', 0) == 0)
            continue;

        StoredSession session;
        session.name = dir.path().filename().string();

        for (const auto& entry : fs::directory_iterator(dir.path(), ec))
        {
            if (!entry.is_regular_file())
                continue;
            StoredPhoto photo;
            photo.file = entry.path().filename().string();
            std::smatch match;
            if (std::regex_match(photo.file, match, photoName))
            {
                photo.order = std::stoi(match[1].str());
                photo.camera = match[2].str();
            }
            photo.previewable = IsJpeg(photo.file);
            session.photos.push_back(photo);
        }

        std::sort(session.photos.begin(), session.photos.end(),
            [](const StoredPhoto& a, const StoredPhoto& b)
            { return a.order != b.order ? a.order < b.order : a.file < b.file; });

        if (!session.photos.empty())
            sessions.push_back(session);
    }

    // Mais recente primeiro (nomes começam com data e hora)
    std::sort(sessions.begin(), sessions.end(),
        [](const StoredSession& a, const StoredSession& b) { return a.name > b.name; });
    return sessions;
}

int PhotoStore::CountSessions() const
{
    return static_cast<int>(ListSessions().size());
}

std::string PhotoStore::Preview(const std::string& session, const std::string& file, int maxSize) const
{
    const fs::path dir = SessionPath(session);
    if (dir.empty() || file.find_first_of("/\\") != std::string::npos || !IsJpeg(file))
        return "";

    const fs::path cache = dir / kPreviewFolder / (std::to_string(maxSize) + "_" + file);
    std::error_code ec;
    if (fs::exists(cache, ec))
        return ReadFile(cache);

    const std::string original = ReadFile(dir / file);
    if (original.empty())
        return "";

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(original.data()), static_cast<int>(original.size()), &w, &h, &channels, 3);
    if (!pixels)
        return "";

    std::vector<unsigned char> image(pixels, pixels + static_cast<size_t>(w) * h * 3);
    stbi_image_free(pixels);

    image = Rotate(image, w, h, ReadExifOrientation(original));

    const double scale = std::min(1.0, static_cast<double>(maxSize) / std::max(w, h));
    const int tw = std::max(1, static_cast<int>(w * scale));
    const int th = std::max(1, static_cast<int>(h * scale));
    std::vector<unsigned char> resized(static_cast<size_t>(tw) * th * 3);
    stbir_resize_uint8_linear(image.data(), w, h, 0, resized.data(), tw, th, 0, STBIR_RGB);

    std::string jpeg;
    stbi_write_jpg_to_func(AppendToString, &jpeg, tw, th, 3, resized.data(), 85);

    fs::create_directories(cache.parent_path(), ec);
    std::ofstream(cache, std::ios::binary) << jpeg;
    return jpeg;
}

int PhotoStore::SaveAll(const fs::path& destination, std::string& errorMessage) const
{
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec)
    {
        errorMessage = "Could not open the folder: " + ec.message();
        return 0;
    }

    int moved = 0;
    for (const auto& session : ListSessions())
    {
        const fs::path from = root / session.name;
        fs::remove_all(from / kPreviewFolder, ec);

        // Não sobrescreve: "nome", "nome (2)", "nome (3)"...
        fs::path to = destination / session.name;
        for (int n = 2; fs::exists(to, ec); ++n)
            to = destination / (session.name + " (" + std::to_string(n) + ")");

        fs::rename(from, to, ec);
        if (ec)
        {
            // Outro disco: copia e depois apaga
            ec.clear();
            fs::copy(from, to, fs::copy_options::recursive, ec);
            if (ec)
            {
                errorMessage = "Could not save " + session.name + ": " + ec.message();
                return moved;
            }
            fs::remove_all(from, ec);
        }
        ++moved;
    }
    return moved;
}

bool PhotoStore::DeleteSession(const std::string& session) const
{
    const fs::path dir = SessionPath(session);
    std::error_code ec;
    return !dir.empty() && fs::remove_all(dir, ec) > 0 && !ec;
}

void PhotoStore::DeleteAll() const
{
    std::error_code ec;
    for (const auto& session : ListSessions())
        fs::remove_all(root / session.name, ec);
}

std::string Base64Encode(const std::string& data)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3)
    {
        const unsigned n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) | static_cast<unsigned char>(data[i + 2]);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += table[n & 63];
    }
    if (i < data.size())
    {
        unsigned n = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size())
            n |= static_cast<unsigned char>(data[i + 1]) << 8;
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += (i + 1 < data.size()) ? table[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}
