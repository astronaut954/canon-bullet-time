#include "match_debug.h"

#include "photo_store.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace
{
    std::string Now(const char* format)
    {
        const std::time_t t = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        char text[32];
        std::strftime(text, sizeof(text), format, &local);
        return text;
    }

    // Campo de CSV (entre aspas quando tem vírgula, aspas ou quebra de linha)
    std::string Csv(const std::string& value)
    {
        if (value.find_first_of(",\"\n") == std::string::npos)
            return value;
        std::string out = "\"";
        for (char ch : value)
            out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
        return out + "\"";
    }

    // (Não "Fixed": o macOS já tem um tipo com esse nome)
    std::string Decimals(double v, int decimals)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.*f", decimals, v);
        return text;
    }

    std::string LabelOf(const CameraSettings& s, const char* key)
    {
        for (const auto& c : s.settings)
            if (c.key == key)
                return c.available ? c.label : "";
        return "";
    }

    // Apaga as pastas mais antigas até sobrarem no máximo "keep" e "maxBytes" no total
    void Prune(const std::filesystem::path& root, size_t keep, std::uintmax_t maxBytes)
    {
        std::error_code ec;
        std::vector<std::filesystem::path> runs;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
            if (entry.is_directory(ec))
                runs.push_back(entry.path());
        std::sort(runs.begin(), runs.end());  // nomes começam pela data: os mais velhos primeiro
        std::uintmax_t total = 0;
        std::vector<std::uintmax_t> sizes;
        for (const auto& run : runs)
        {
            sizes.push_back(MatchDebug::FolderSize(run));
            total += sizes.back();
        }
        for (size_t i = 0; i < runs.size(); ++i)
        {
            if (runs.size() - i <= keep && total <= maxBytes)
                break;
            std::filesystem::remove_all(runs[i], ec);
            total -= sizes[i];
        }
    }
}

bool MatchDebug::Begin(const std::filesystem::path& root, const std::string& kind)
{
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    Prune(root, 19, 250ull * 1024 * 1024);  // deixa espaço para esta (até ~300 MB no total)
    std::filesystem::path dir = root / (Now("%Y-%m-%d_%H-%M-%S") + "_" + kind);
    for (int n = 2; std::filesystem::exists(dir, ec); ++n)
        dir = root / (Now("%Y-%m-%d_%H-%M-%S") + "_" + kind + "_" + std::to_string(n));
    if (!std::filesystem::create_directories(dir, ec))
        return false;
    folder = dir;
    std::ofstream csv(folder / "report.csv");
    csv << "photo,stage,camera,model,mode,iso,shutter,aperture,exp_comp,white_balance,kelvin,fine_tune_ab,fine_tune_gm,"
           "picture_style,contrast_setting,saturation_setting,color_tone_setting,measured,brightness_ev,color_rb,color_g,"
           "contrast_stops,saturation_ln,dark_pct,clipped_pct,brightness_reliable,file\n";
    Line("Stillpoint debug: " + Now("%Y-%m-%d %H:%M:%S") + " (" + kind + ")");
    return static_cast<bool>(csv);
}

void MatchDebug::Shot(int shot, const std::string& stage, const std::vector<Photo>& photos)
{
    if (!Active())
        return;
    Line("Photo " + std::to_string(shot) + ": " + stage);
    std::ofstream csv(folder / "report.csv", std::ios::app);
    for (const auto& p : photos)
    {
        // A prévia que o app mediu (em RAW, a prévia JPEG embutida pela câmera)
        std::string saved;
        if (!p.file.empty())
        {
            RgbFrame frame;
            std::string jpeg;
            const std::string name = "photo-" + std::to_string(shot) + "_" + p.camera + ".jpg";
            if (LoadPhotoRgb(p.file, frame, &jpeg) && !jpeg.empty())
            {
                std::ofstream out(folder / name, std::ios::binary);
                out.write(jpeg.data(), static_cast<std::streamsize>(jpeg.size()));
                saved = name;
            }
        }
        const CameraSettings& s = p.settings;
        const ColorMatch::Stats& m = p.measured;
        std::string wb = LabelOf(s, "wb");
        csv << shot << ',' << Csv(stage) << ',' << Csv(p.camera) << ',' << Csv(s.model) << ',' << Csv(s.mode) << ','
            << Csv(LabelOf(s, "iso")) << ',' << Csv(LabelOf(s, "tv")) << ',' << Csv(LabelOf(s, "av")) << ','
            << Csv(LabelOf(s, "expcomp")) << ',' << Csv(wb) << ',' << (wb == "Kelvin" ? std::to_string(s.kelvin) : "") << ','
            << (s.shiftAvailable ? std::to_string(s.shiftAB) : "") << ',' << (s.shiftAvailable ? std::to_string(s.shiftGM) : "") << ','
            << Csv(LabelOf(s, "style")) << ','
            << (s.styleDetailAvailable ? std::to_string(s.contrast) : "") << ','
            << (s.styleDetailAvailable ? std::to_string(s.saturation) : "") << ','
            << (s.styleDetailAvailable ? std::to_string(s.colorTone) : "") << ','
            << (m.ok ? "yes" : "no") << ','
            << (m.ok ? Decimals(m.ev, 3) : "") << ',' << (m.ok ? Decimals(m.rb, 4) : "") << ',' << (m.ok ? Decimals(m.g, 4) : "") << ','
            << (m.ok ? Decimals(m.contrast, 3) : "") << ',' << (m.ok ? Decimals(m.sat, 4) : "") << ','
            << (m.ok ? std::to_string(std::lround(m.dark * 100)) : "") << ','
            << (m.ok ? std::to_string(std::lround(m.clipped * 100)) : "") << ','
            << (m.ok ? (m.exposureOk ? "yes" : "no") : "") << ',' << Csv(saved) << '\n';
    }
}

std::uintmax_t MatchDebug::FolderSize(const std::filesystem::path& folder)
{
    std::error_code ec;
    std::uintmax_t total = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec))
            total += it->file_size(ec);
    return total;
}

void MatchDebug::Line(const std::string& text)
{
    if (!Active())
        return;
    std::ofstream out(folder / "trace.txt", std::ios::app);
    out << text << '\n';
}
