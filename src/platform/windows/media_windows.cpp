// Mídia no Windows: vídeo H.264 pelo codificador do Media Foundation (vem com o
// Windows 10/11) e HEIC pelo WIC (precisa das extensões HEIF e HEVC da Microsoft).

#define NOMINMAX
#include "platform/media.h"

#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <codecapi.h>
#include <strmif.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

namespace Platform
{
    namespace
    {
        // {E1E62521-6787-405B-A339-500715B5763F}
        const GUID kContainerHeif = { 0xe1e62521, 0x6787, 0x405b, { 0xa3, 0x39, 0x50, 0x07, 0x15, 0xb5, 0x76, 0x3f } };

        template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

        // COM e Media Foundation na thread que exporta (uma vez por thread)
        void StartCom()
        {
            thread_local bool started = false;
            if (started)
                return;
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            MFStartup(MF_VERSION, MFSTARTUP_LITE);
            started = true;
        }

        // RGB -> NV12 (BT.709, faixa de vídeo), o formato que o codificador recebe
        void RgbToNv12(const unsigned char* rgb, int w, int h, unsigned char* out)
        {
            unsigned char* yPlane = out;
            unsigned char* uv = out + static_cast<size_t>(w) * h;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const unsigned char* p = rgb + (static_cast<size_t>(y) * w + x) * 3;
                    const int Y = (47 * p[0] + 157 * p[1] + 16 * p[2] + 128) / 256 + 16;
                    yPlane[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(std::clamp(Y, 0, 255));
                }
            for (int y = 0; y < h; y += 2)
                for (int x = 0; x < w; x += 2)
                {
                    int r = 0, g = 0, b = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                        {
                            const unsigned char* p = rgb + (static_cast<size_t>(y + dy) * w + x + dx) * 3;
                            r += p[0]; g += p[1]; b += p[2];
                        }
                    r /= 4; g /= 4; b /= 4;
                    const int U = (-26 * r - 87 * g + 112 * b + 128) / 256 + 128;
                    const int V = (112 * r - 102 * g - 10 * b + 128) / 256 + 128;
                    unsigned char* d = uv + static_cast<size_t>(y / 2) * w + x;
                    d[0] = static_cast<unsigned char>(std::clamp(U, 0, 255));
                    d[1] = static_cast<unsigned char>(std::clamp(V, 0, 255));
                }
        }

        class MfH264 : public H264Encoder
        {
        public:
            ~MfH264() override { Release(transform); }

            bool Start(int width, int height, int fps, std::string& error)
            {
                w = width; h = height; rate = fps;
                MFT_REGISTER_TYPE_INFO input{ MFMediaType_Video, MFVideoFormat_NV12 };
                MFT_REGISTER_TYPE_INFO output{ MFMediaType_Video, MFVideoFormat_H264 };
                IMFActivate** list = nullptr;
                UINT32 count = 0;
                HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                                       &input, &output, &list, &count);
                if (SUCCEEDED(hr) && count > 0)
                    hr = list[0]->ActivateObject(IID_PPV_ARGS(&transform));
                for (UINT32 i = 0; i < count; ++i)
                    list[i]->Release();
                CoTaskMemFree(list);
                if (FAILED(hr) || !transform)
                {
                    error = "Windows has no H.264 video encoder (Windows N editions need the Media Feature Pack).";
                    return false;
                }

                IMFMediaType* out = nullptr;
                MFCreateMediaType(&out);
                out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(std::min<long long>(
                    static_cast<long long>(w) * h * fps / 4 * 3 / 2 + 2000000, 60000000)));
                MFSetAttributeSize(out, MF_MT_FRAME_SIZE, w, h);
                MFSetAttributeRatio(out, MF_MT_FRAME_RATE, fps, 1);
                MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
                hr = transform->SetOutputType(0, out, 0);
                Release(out);

                IMFMediaType* in = nullptr;
                MFCreateMediaType(&in);
                in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                MFSetAttributeSize(in, MF_MT_FRAME_SIZE, w, h);
                MFSetAttributeRatio(in, MF_MT_FRAME_RATE, fps, 1);
                MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                if (SUCCEEDED(hr))
                    hr = transform->SetInputType(0, in, 0);
                Release(in);
                if (FAILED(hr))
                {
                    error = "The H.264 encoder does not accept " + std::to_string(w) + " x " + std::to_string(h) + ".";
                    return false;
                }

                // Sem quadros B nem descarte de quadros: um quadro de saída por foto, na ordem.
                // (Por padrão o codificador pode reordenar quadros e, para segurar a taxa,
                // pular alguns, o que o arquivo MP4/MOV mostrava como fotos sumindo e tempos
                // diferentes.) Qualidade constante em vez de taxa fixa.
                ICodecAPI* codec = nullptr;
                if (SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(&codec))))
                {
                    auto set = [&](const GUID& key, ULONG value)
                    {
                        VARIANT v;
                        VariantInit(&v);
                        v.vt = VT_UI4;
                        v.ulVal = value;
                        codec->SetValue(&key, &v);
                    };
                    auto setBool = [&](const GUID& key, bool value)
                    {
                        VARIANT v;
                        VariantInit(&v);
                        v.vt = VT_BOOL;
                        v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
                        codec->SetValue(&key, &v);
                    };
                    setBool(CODECAPI_AVLowLatencyMode, true);
                    set(CODECAPI_AVEncMPVDefaultBPictureCount, 0);
                    set(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_Quality);
                    set(CODECAPI_AVEncCommonQuality, 85);
                    set(CODECAPI_AVEncMPVGOPSize, static_cast<ULONG>(fps * 2));
                    codec->Release();
                }

                // Parâmetros do vídeo (SPS/PPS), caso o codificador não os coloque no fluxo
                IMFMediaType* current = nullptr;
                if (SUCCEEDED(transform->GetOutputCurrentType(0, &current)))
                {
                    UINT32 size = 0;
                    if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size > 0)
                    {
                        header.resize(size);
                        current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.data(), size, nullptr);
                    }
                    Release(current);
                }
                transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
                transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
                return true;
            }

            bool Encode(const unsigned char* rgb, std::vector<unsigned char>& out, std::string& error) override
            {
                const DWORD size = static_cast<DWORD>(w) * h * 3 / 2;
                IMFMediaBuffer* buffer = nullptr;
                IMFSample* sample = nullptr;
                HRESULT hr = MFCreateMemoryBuffer(size, &buffer);
                BYTE* data = nullptr;
                if (SUCCEEDED(hr) && SUCCEEDED(hr = buffer->Lock(&data, nullptr, nullptr)))
                {
                    RgbToNv12(rgb, w, h, data);
                    buffer->Unlock();
                    buffer->SetCurrentLength(size);
                }
                if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
                if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
                const LONGLONG duration = 10000000LL / rate;
                if (SUCCEEDED(hr)) sample->SetSampleTime(frame * duration);
                if (SUCCEEDED(hr)) sample->SetSampleDuration(duration);
                if (SUCCEEDED(hr)) hr = transform->ProcessInput(0, sample, 0);
                Release(sample);
                Release(buffer);
                ++frame;
                if (FAILED(hr))
                {
                    error = "The video encoder stopped (frame " + std::to_string(frame) + ").";
                    return false;
                }
                return Drain(out, error);
            }

            bool Finish(std::vector<unsigned char>& out, std::string& error) override
            {
                transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
                transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
                return Drain(out, error);
            }

        private:
            bool Drain(std::vector<unsigned char>& out, std::string& error)
            {
                MFT_OUTPUT_STREAM_INFO info{};
                transform->GetOutputStreamInfo(0, &info);
                const bool provides = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
                for (;;)
                {
                    MFT_OUTPUT_DATA_BUFFER output{};
                    IMFSample* sample = nullptr;
                    IMFMediaBuffer* buffer = nullptr;
                    if (!provides)
                    {
                        MFCreateSample(&sample);
                        MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, static_cast<DWORD>(w) * h * 2), &buffer);
                        sample->AddBuffer(buffer);
                        output.pSample = sample;
                    }
                    DWORD status = 0;
                    HRESULT hr = transform->ProcessOutput(0, 1, &output, &status);
                    if (output.pEvents)
                        output.pEvents->Release();
                    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
                    {
                        Release(buffer);
                        Release(sample);
                        return true;
                    }
                    if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
                    {
                        IMFMediaType* type = nullptr;
                        if (SUCCEEDED(transform->GetOutputAvailableType(0, 0, &type)))
                        {
                            transform->SetOutputType(0, type, 0);
                            Release(type);
                        }
                        Release(buffer);
                        Release(sample);
                        continue;
                    }
                    if (FAILED(hr))
                    {
                        Release(buffer);
                        Release(sample);
                        error = "The video encoder failed.";
                        return false;
                    }
                    IMFMediaBuffer* contiguous = nullptr;
                    if (output.pSample && SUCCEEDED(output.pSample->ConvertToContiguousBuffer(&contiguous)))
                    {
                        BYTE* data = nullptr;
                        DWORD length = 0;
                        if (SUCCEEDED(contiguous->Lock(&data, nullptr, &length)))
                        {
                            // Primeiro quadro sem SPS (NAL 7): coloca os parâmetros antes
                            if (first && !header.empty())
                            {
                                bool hasSps = false;
                                for (DWORD i = 0; i + 4 < length && !hasSps; ++i)
                                    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && (data[i + 3] & 0x1F) == 7)
                                        hasSps = true;
                                if (!hasSps)
                                    out.insert(out.end(), header.begin(), header.end());
                            }
                            first = false;
                            out.insert(out.end(), data, data + length);
                            contiguous->Unlock();
                        }
                        contiguous->Release();
                    }
                    if (provides && output.pSample)
                        output.pSample->Release();
                    Release(buffer);
                    Release(sample);
                }
            }

            IMFTransform* transform = nullptr;
            std::vector<unsigned char> header;
            int w = 0, h = 0, rate = 30;
            LONGLONG frame = 0;
            bool first = true;
        };

        IWICImagingFactory* Factory()
        {
            StartCom();
            IWICImagingFactory* factory = nullptr;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            return factory;
        }
    }

    namespace
    {
        // MP4 gravado pelo Sink Writer do Media Foundation: ele mesmo configura o
        // codificador, ordena os quadros e monta o arquivo.
        class MfFileWriter : public VideoFileWriter
        {
        public:
            ~MfFileWriter() override { Release(writer); }

            bool Start(const std::filesystem::path& file, int width, int height, int fps, std::string& error)
            {
                w = width; h = height; rate = fps;
                IMFAttributes* attributes = nullptr;
                MFCreateAttributes(&attributes, 2);
                attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
                attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
                attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
                HRESULT hr = MFCreateSinkWriterFromURL(file.wstring().c_str(), nullptr, attributes, &writer);
                Release(attributes);

                IMFMediaType* out = nullptr;
                if (SUCCEEDED(hr)) hr = MFCreateMediaType(&out);
                if (SUCCEEDED(hr))
                {
                    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                    out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(std::min<long long>(
                        static_cast<long long>(w) * h * fps / 4 * 3 / 2 * 2 + 4000000, 80000000)));
                    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
                    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, w, h);
                    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, fps, 1);
                    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                    hr = writer->AddStream(out, &stream);
                }
                Release(out);

                IMFMediaType* in = nullptr;
                if (SUCCEEDED(hr)) hr = MFCreateMediaType(&in);
                if (SUCCEEDED(hr))
                {
                    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, w, h);
                    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, fps, 1);
                    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                    hr = writer->SetInputMediaType(stream, in, nullptr);
                }
                Release(in);
                if (SUCCEEDED(hr)) hr = writer->BeginWriting();
                if (FAILED(hr))
                {
                    error = "Windows could not start the video (" + std::to_string(static_cast<unsigned long>(hr)) + ").";
                    return false;
                }
                return true;
            }

            bool AddFrame(const unsigned char* rgb, std::string& error) override
            {
                const DWORD size = static_cast<DWORD>(w) * h * 3 / 2;
                IMFMediaBuffer* buffer = nullptr;
                IMFSample* sample = nullptr;
                HRESULT hr = MFCreateMemoryBuffer(size, &buffer);
                BYTE* data = nullptr;
                if (SUCCEEDED(hr) && SUCCEEDED(hr = buffer->Lock(&data, nullptr, nullptr)))
                {
                    RgbToNv12(rgb, w, h, data);
                    buffer->Unlock();
                    buffer->SetCurrentLength(size);
                }
                if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
                if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
                const LONGLONG duration = 10000000LL / rate;
                if (SUCCEEDED(hr)) hr = sample->SetSampleTime(frame * duration);
                if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration);
                if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample);
                Release(sample);
                Release(buffer);
                ++frame;
                if (FAILED(hr))
                {
                    error = "Windows could not write the video (frame " + std::to_string(frame) + ").";
                    return false;
                }
                return true;
            }

            bool Finish(std::string& error) override
            {
                const HRESULT hr = writer->Finalize();
                Release(writer);
                if (FAILED(hr))
                {
                    error = "Windows could not finish the video.";
                    return false;
                }
                return true;
            }

        private:
            IMFSinkWriter* writer = nullptr;
            DWORD stream = 0;
            int w = 0, h = 0, rate = 30;
            LONGLONG frame = 0;
        };
    }

    std::unique_ptr<VideoFileWriter> CreateVideoFileWriter(const std::filesystem::path& file, int width, int height,
                                                           int fps, std::string& error)
    {
        StartCom();
        auto writer = std::make_unique<MfFileWriter>();
        if (!writer->Start(file, width, height, fps, error))
            return nullptr;
        return writer;
    }

    std::unique_ptr<H264Encoder> CreateH264Encoder(int width, int height, int fps, std::string& error)
    {
        StartCom();
        auto encoder = std::make_unique<MfH264>();
        if (!encoder->Start(width, height, fps, error))
            return nullptr;
        return encoder;
    }

    bool HeicAvailable(std::string& reason)
    {
        StartCom();
        MFT_REGISTER_TYPE_INFO output{ MFMediaType_Video, MFVideoFormat_HEVC };
        IMFActivate** list = nullptr;
        UINT32 count = 0;
        if (SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_ALL, nullptr, &output, &list, &count)))
        {
            for (UINT32 i = 0; i < count; ++i)
                list[i]->Release();
            CoTaskMemFree(list);
        }
        IWICImagingFactory* factory = Factory();
        IWICBitmapEncoder* encoder = nullptr;
        const bool heif = factory && SUCCEEDED(factory->CreateEncoder(kContainerHeif, nullptr, &encoder));
        Release(encoder);
        Release(factory);
        if (count > 0 && heif)
            return true;
        reason = "Needs Microsoft's HEIF Image Extensions and HEVC Video Extensions (Microsoft Store).";
        return false;
    }

    bool WriteHeic(const unsigned char* rgb, int width, int height, const std::filesystem::path& file,
                   std::string& error)
    {
        IWICImagingFactory* factory = Factory();
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* props = nullptr;
        HRESULT hr = factory ? factory->CreateStream(&stream) : E_FAIL;
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(file.wstring().c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = factory->CreateEncoder(kContainerHeif, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
        if (SUCCEEDED(hr)) hr = frame->Initialize(props);
        if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
        if (SUCCEEDED(hr))
        {
            std::vector<unsigned char> bgr(static_cast<size_t>(width) * height * 3);
            for (size_t i = 0; i < bgr.size(); i += 3)
            {
                bgr[i] = rgb[i + 2];
                bgr[i + 1] = rgb[i + 1];
                bgr[i + 2] = rgb[i];
            }
            hr = frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(width) * 3,
                                    static_cast<UINT>(bgr.size()), bgr.data());
        }
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
        Release(props);
        Release(frame);
        Release(encoder);
        Release(stream);
        Release(factory);
        if (FAILED(hr))
        {
            std::error_code ec;
            std::filesystem::remove(file, ec);
            error = "Could not save " + file.filename().string() + " as HEIC.";
            return false;
        }
        return true;
    }
}
