// Teste da exportação: stillpoint_export_test <pasta da sessão> <pasta de saída> [standard|high|maximum] [altura]
#include "exporter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: stillpoint_export_test <session folder> <output folder>\n");
        return 2;
    }
    std::vector<fs::path> photos;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.is_regular_file())
            photos.push_back(e.path());
    std::sort(photos.begin(), photos.end());
    fs::create_directories(argv[2]);

    Export::SequenceOptions options;
    options.formats = { "mp4", "mov", "gif", "webp", "avif" };
    options.fps = 6;
    options.loops = 3;
    const Export::Quality quality = Export::ParseQuality(argc > 3 ? argv[3] : "high");
    options.quality = quality;
    if (argc > 4)
        options.height = std::atoi(argv[4]);
    std::string error;
    const bool ok = Export::WriteSequence(photos, fs::path(argv[2]) / "sequence", options,
        [](double f, const std::string& step) { std::printf("\r%-6s %3d%%", step.c_str(), static_cast<int>(f * 100)); std::fflush(stdout); },
        nullptr, error);
    std::printf("\nsequence: %s %s\n", ok ? "ok" : "FAILED", error.c_str());
    int failures = ok ? 0 : 1;
    for (const char* format : { "jpg", "png", "webp", "tiff" })
    {
        error.clear();
        const bool converted = Export::ConvertPhoto(photos[0], fs::path(argv[2]) / photos[0].stem(), format, quality, error);
        std::printf("photo %s: %s %s\n", format, converted ? "ok" : "FAILED", error.c_str());
        failures += converted ? 0 : 1;
    }
    return failures;
}
