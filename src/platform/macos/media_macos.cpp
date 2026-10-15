// Mídia no Mac: vídeo H.264 pelo VideoToolbox (o codificador do próprio Mac,
// por hardware nos Macs com Apple Silicon) e HEIC pelo ImageIO.

#include "platform/media.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <ImageIO/ImageIO.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace Platform
{
    namespace
    {
        void SetNumber(VTCompressionSessionRef session, CFStringRef key, int value)
        {
            CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
            VTSessionSetProperty(session, key, number);
            CFRelease(number);
        }

        class VtH264 : public H264Encoder
        {
        public:
            ~VtH264() override
            {
                if (session)
                {
                    VTCompressionSessionInvalidate(session);
                    CFRelease(session);
                }
            }

            bool Start(int width, int height, int fps, std::string& error)
            {
                w = width; h = height; rate = fps;
                OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault, w, h, kCMVideoCodecType_H264, nullptr,
                                                             nullptr, nullptr, &VtH264::OnFrame, this, &session);
                if (status != noErr || !session)
                {
                    error = "The Mac video encoder did not start (" + std::to_string(status) + ").";
                    session = nullptr;
                    return false;
                }
                VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
                VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
                VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
                SetNumber(session, kVTCompressionPropertyKey_AverageBitRate,
                          static_cast<int>(std::min<long long>(static_cast<long long>(w) * h * fps / 4 * 3 / 2 + 2000000, 60000000)));
                SetNumber(session, kVTCompressionPropertyKey_ExpectedFrameRate, fps);
                SetNumber(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, fps * 2);
                VTCompressionSessionPrepareToEncodeFrames(session);
                return true;
            }

            bool Encode(const unsigned char* rgb, std::vector<unsigned char>& out, std::string& error) override
            {
                CVPixelBufferRef pixels = nullptr;
                if (CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA, nullptr, &pixels) != kCVReturnSuccess)
                {
                    error = "Out of memory for the video.";
                    return false;
                }
                CVPixelBufferLockBaseAddress(pixels, 0);
                auto* base = static_cast<unsigned char*>(CVPixelBufferGetBaseAddress(pixels));
                const size_t stride = CVPixelBufferGetBytesPerRow(pixels);
                for (int y = 0; y < h; ++y)
                {
                    unsigned char* row = base + stride * y;
                    const unsigned char* src = rgb + static_cast<size_t>(y) * w * 3;
                    for (int x = 0; x < w; ++x)
                    {
                        row[x * 4] = src[x * 3 + 2];
                        row[x * 4 + 1] = src[x * 3 + 1];
                        row[x * 4 + 2] = src[x * 3];
                        row[x * 4 + 3] = 255;
                    }
                }
                CVPixelBufferUnlockBaseAddress(pixels, 0);
                const OSStatus status = VTCompressionSessionEncodeFrame(session, pixels, CMTimeMake(frame++, rate),
                                                                        CMTimeMake(1, rate), nullptr, nullptr, nullptr);
                CVPixelBufferRelease(pixels);
                if (status != noErr || failed)
                {
                    error = "The Mac video encoder failed (" + std::to_string(status) + ").";
                    return false;
                }
                Take(out);
                return true;
            }

            bool Finish(std::vector<unsigned char>& out, std::string& error) override
            {
                VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
                if (failed)
                {
                    error = "The Mac video encoder failed.";
                    return false;
                }
                Take(out);
                return true;
            }

        private:
            static void OnFrame(void* ref, void*, OSStatus status, VTEncodeInfoFlags, CMSampleBufferRef sample)
            {
                auto* self = static_cast<VtH264*>(ref);
                if (status != noErr || !sample)
                {
                    self->failed = true;
                    return;
                }
                self->Append(sample);
            }

            // Converte o quadro (NALs com tamanho na frente) para Annex-B (00 00 00 01 + NAL)
            void Append(CMSampleBufferRef sample)
            {
                static const unsigned char start[] = { 0, 0, 0, 1 };
                std::vector<unsigned char> bytes;
                bool keyframe = true;
                CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
                if (attachments && CFArrayGetCount(attachments) > 0)
                {
                    auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
                    keyframe = !CFDictionaryContainsKey(dict, kCMSampleAttachmentKey_NotSync);
                }
                int lengthSize = 4;
                if (keyframe)
                {
                    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
                    size_t count = 0;
                    if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &lengthSize) == noErr)
                        for (size_t i = 0; i < count; ++i)
                        {
                            const uint8_t* data = nullptr;
                            size_t size = 0;
                            if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &data, &size, nullptr, nullptr) == noErr)
                            {
                                bytes.insert(bytes.end(), start, start + 4);
                                bytes.insert(bytes.end(), data, data + size);
                            }
                        }
                }
                CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
                const size_t total = block ? CMBlockBufferGetDataLength(block) : 0;
                std::vector<unsigned char> data(total);
                if (total && CMBlockBufferCopyDataBytes(block, 0, total, data.data()) == kCMBlockBufferNoErr)
                {
                    size_t pos = 0;
                    while (pos + static_cast<size_t>(lengthSize) <= total)
                    {
                        size_t nal = 0;
                        for (int k = 0; k < lengthSize; ++k)
                            nal = (nal << 8) | data[pos + k];
                        pos += static_cast<size_t>(lengthSize);
                        if (nal == 0 || pos + nal > total)
                            break;
                        bytes.insert(bytes.end(), start, start + 4);
                        bytes.insert(bytes.end(), data.begin() + static_cast<long>(pos), data.begin() + static_cast<long>(pos + nal));
                        pos += nal;
                    }
                }
                std::lock_guard<std::mutex> lock(mutex);
                pending.insert(pending.end(), bytes.begin(), bytes.end());
            }

            void Take(std::vector<unsigned char>& out)
            {
                std::lock_guard<std::mutex> lock(mutex);
                out.insert(out.end(), pending.begin(), pending.end());
                pending.clear();
            }

            VTCompressionSessionRef session = nullptr;
            std::mutex mutex;
            std::vector<unsigned char> pending;
            int w = 0, h = 0, rate = 30;
            int64_t frame = 0;
            bool failed = false;
        };
    }

    std::unique_ptr<H264Encoder> CreateH264Encoder(int width, int height, int fps, std::string& error)
    {
        auto encoder = std::make_unique<VtH264>();
        if (!encoder->Start(width, height, fps, error))
            return nullptr;
        return encoder;
    }

    bool HeicAvailable(std::string& reason)
    {
        CFArrayRef types = CGImageDestinationCopyTypeIdentifiers();
        const bool ok = types && CFArrayContainsValue(types, CFRangeMake(0, CFArrayGetCount(types)), CFSTR("public.heic"));
        if (types)
            CFRelease(types);
        if (!ok)
            reason = "This Mac cannot save HEIC.";
        return ok;
    }

    bool WriteHeic(const unsigned char* rgb, int width, int height, const std::filesystem::path& file,
                   std::string& error)
    {
        const std::string path = file.string();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path.c_str()),
                                                               static_cast<CFIndex>(path.size()), false);
        CGImageDestinationRef destination = url ? CGImageDestinationCreateWithURL(url, CFSTR("public.heic"), 1, nullptr) : nullptr;
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, rgb, static_cast<size_t>(width) * height * 3, nullptr);
        CGImageRef image = CGImageCreate(width, height, 8, 24, static_cast<size_t>(width) * 3, space,
                                         kCGBitmapByteOrderDefault | kCGImageAlphaNone, provider, nullptr, false,
                                         kCGRenderingIntentDefault);
        bool ok = false;
        if (destination && image)
        {
            float quality = 0.9f;
            CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &quality);
            const void* keys[] = { kCGImageDestinationLossyCompressionQuality };
            const void* values[] = { number };
            CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
            CGImageDestinationAddImage(destination, image, options);
            ok = CGImageDestinationFinalize(destination);
            CFRelease(options);
            CFRelease(number);
        }
        if (image) CGImageRelease(image);
        if (provider) CGDataProviderRelease(provider);
        if (space) CGColorSpaceRelease(space);
        if (destination) CFRelease(destination);
        if (url) CFRelease(url);
        if (!ok)
            error = "Could not save " + file.filename().string() + " as HEIC.";
        return ok;
    }

    std::unique_ptr<VideoFileWriter> CreateVideoFileWriter(const std::filesystem::path&, int, int, int, std::string&)
    {
        return nullptr;  // usa o H264Encoder + minimp4
    }
}
