#include "color_match.h"

#include "photo_store.h"

#include <algorithm>
#include <cmath>

namespace ColorMatch
{
    Stats Measure(const std::filesystem::path& photo)
    {
        Stats out;
        RgbFrame frame;
        if (!LoadPhotoRgb(photo, frame) || frame.width < 8 || frame.height < 8)
            return out;

        // sRGB (0–255) para luz linear
        static double linear[256];
        static bool ready = false;
        if (!ready)
        {
            for (int i = 0; i < 256; ++i)
            {
                const double c = i / 255.0;
                linear[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            }
            ready = true;
        }

        // Brilho como o olho vê a foto: claridade média (L*, escala perceptiva) da foto
        // inteira, com o centro valendo o dobro. (A mediana do centro pulava de um tom para
        // outro em fotos meio claras, meio escuras, e enganava o ajuste do ISO.)
        {
            const int fullStep = std::max(1, std::max(frame.width, frame.height) / 400);
            double sumL = 0, weights = 0;
            long count = 0, nearBlack = 0, blown = 0;
            for (int y = 0; y < frame.height; y += fullStep)
                for (int x = 0; x < frame.width; x += fullStep)
                {
                    const unsigned char* p = &frame.pixels[(static_cast<size_t>(y) * frame.width + x) * 3];
                    const double lum = 0.2126 * linear[p[0]] + 0.7152 * linear[p[1]] + 0.0722 * linear[p[2]];
                    const double f = lum > 0.008856 ? std::cbrt(lum) : 7.787 * lum + 16.0 / 116.0;
                    const double lightness = 116.0 * f - 16.0;  // 0 (preto) a 100 (branco)
                    const bool center = x >= frame.width / 4 && x < frame.width - frame.width / 4
                                        && y >= frame.height / 4 && y < frame.height - frame.height / 4;
                    const double w = center ? 2.0 : 1.0;
                    sumL += w * lightness;
                    weights += w;
                    ++count;
                    if (lum < 0.004)
                        ++nearBlack;
                    if (p[0] >= 250 || p[1] >= 250 || p[2] >= 250)
                        ++blown;
                }
            if (count == 0 || weights <= 0)
                return out;
            // Claridade média de volta para luz, em stops (1 stop de ISO ≈ 1 stop aqui nos meios-tons)
            const double meanL = sumL / weights;
            const double f = (meanL + 16.0) / 116.0;
            const double meanLum = f > 0.206893 ? f * f * f : (f - 16.0 / 116.0) / 7.787;
            out.ev = std::log2(std::max(1e-6, meanLum));
            out.dark = static_cast<double>(nearBlack) / count;
            out.clipped = static_cast<double>(blown) / count;
            out.exposureOk = out.dark < 0.7 && out.clipped < 0.5;
        }

        // Cor, contraste e saturação: centro (metade da largura e da altura), em até ~400×400 pontos
        const int x0 = frame.width / 4, x1 = frame.width - frame.width / 4;
        const int y0 = frame.height / 4, y1 = frame.height - frame.height / 4;
        const int step = std::max(1, std::max(x1 - x0, y1 - y0) / 400);
        double sumR = 0, sumG = 0, sumB = 0, sumSat = 0;
        long colorCount = 0, total = 0, satCount = 0;
        std::vector<double> luma;
        luma.reserve(static_cast<size_t>((x1 - x0) / step + 1) * ((y1 - y0) / step + 1));
        for (int y = y0; y < y1; y += step)
            for (int x = x0; x < x1; x += step)
            {
                const unsigned char* p = &frame.pixels[(static_cast<size_t>(y) * frame.width + x) * 3];
                const double r = linear[p[0]], g = linear[p[1]], b = linear[p[2]];
                const double y709 = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                luma.push_back(y709);
                ++total;
                // Cor: sem o estourado (algum canal no máximo) e sem o quase preto
                if (p[0] >= 250 || p[1] >= 250 || p[2] >= 250 || y709 < 0.004)
                    continue;
                // Saturação como se vê (valores da foto, não luz linear), sem os quase pretos
                const int hi = std::max({ p[0], p[1], p[2] }), lo = std::min({ p[0], p[1], p[2] });
                if (hi >= 20)
                {
                    sumSat += static_cast<double>(hi - lo) / hi;
                    ++satCount;
                }
                sumR += r; sumG += g; sumB += b;
                ++colorCount;
            }
        if (total == 0 || colorCount < total / 50 || sumR <= 0 || sumG <= 0 || sumB <= 0)
            return out;
        std::sort(luma.begin(), luma.end());
        const double dark = std::max(1e-4, luma[luma.size() / 10]);
        const double bright = std::max(dark, luma[luma.size() * 9 / 10]);

        out.rb = std::log(sumR / sumB);
        out.g = std::log(sumG / std::sqrt(sumR * sumB));
        out.contrast = std::log2(bright / dark);
        out.sat = std::log(std::max(1e-3, satCount ? sumSat / satCount : 0.0));
        out.ok = true;
        return out;
    }

    Stats Average(const std::vector<Stats>& all)
    {
        Stats avg;
        int n = 0, nExposure = 0;
        const bool anyExposureOk = std::any_of(all.begin(), all.end(), [](const Stats& s) { return s.ok && s.exposureOk; });
        for (const auto& s : all)
        {
            if (!s.ok)
                continue;
            avg.rb += s.rb; avg.g += s.g; avg.sat += s.sat;
            ++n;
            if (s.exposureOk || !anyExposureOk)
            {
                avg.ev += s.ev; avg.contrast += s.contrast;
                ++nExposure;
            }
        }
        if (n == 0)
            return avg;
        avg.rb /= n; avg.g /= n; avg.sat /= n;
        avg.ev /= nExposure; avg.contrast /= nExposure;
        avg.ok = true;
        return avg;
    }
}
