// Só para o teste da exportação no Linux: H.264 pelo ffmpeg instalado na máquina
// de teste (o app de verdade usa VideoToolbox no Mac e Media Foundation no Windows).

#include "platform/media.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace Platform
{
    namespace
    {
        class FfmpegH264 : public H264Encoder
        {
        public:
            FfmpegH264(int w, int h, int fps, int bitrate) : w(w), h(h)
            {
                const std::string cmd = "ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s " + std::to_string(w) + "x"
                                        + std::to_string(h) + " -r " + std::to_string(fps)
                                        + " -i - -c:v libx264 -bf 0 -b:v " + std::to_string(bitrate) + " -pix_fmt yuv420p -f h264 " + temp;
                pipe = popen(cmd.c_str(), "w");
            }
            bool Encode(const unsigned char* rgb, std::vector<unsigned char>&, std::string& error) override
            {
                if (!pipe || fwrite(rgb, 1, static_cast<size_t>(w) * h * 3, pipe) != static_cast<size_t>(w) * h * 3)
                {
                    error = "ffmpeg failed";
                    return false;
                }
                return true;
            }
            bool Finish(std::vector<unsigned char>& out, std::string& error) override
            {
                if (!pipe || pclose(pipe) != 0) { pipe = nullptr; error = "ffmpeg failed"; return false; }
                pipe = nullptr;
                std::ifstream in(temp, std::ios::binary);
                out.assign(std::istreambuf_iterator<char>(in), {});
                std::remove(temp.c_str());
                return !out.empty();
            }
        private:
            int w, h;
            FILE* pipe = nullptr;
            std::string temp = "/tmp/stillpoint-test.h264";
        };
    }

    std::unique_ptr<H264Encoder> CreateH264Encoder(int width, int height, int fps, int bitrate, std::string&)
    {
        return std::make_unique<FfmpegH264>(width, height, fps, bitrate);
    }

    bool HeicAvailable(std::string& reason)
    {
        reason = "HEIC is not available in the Linux test.";
        return false;
    }

    bool WriteHeic(const unsigned char*, int, int, const std::filesystem::path&, float, std::string& error)
    {
        error = "HEIC is not available in the Linux test.";
        return false;
    }

    std::unique_ptr<VideoFileWriter> CreateVideoFileWriter(const std::filesystem::path&, int, int, int, int, std::string&)
    {
        return nullptr;  // usa o H264Encoder + minimp4
    }
}
