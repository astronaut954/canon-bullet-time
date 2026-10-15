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

        // Centro: metade da largura e da altura, amostrado em até ~400×400 pontos
        const int x0 = frame.width / 4, x1 = frame.width - frame.width / 4;
        const int y0 = frame.height / 4, y1 = frame.height - frame.height / 4;
        const int step = std::max(1, std::max(x1 - x0, y1 - y0) / 400);
        double sumR = 0, sumG = 0, sumB = 0;
        long colorCount = 0, total = 0;
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
                sumR += r; sumG += g; sumB += b;
                ++colorCount;
            }
        if (total == 0 || colorCount < total / 50 || sumR <= 0 || sumG <= 0 || sumB <= 0)
            return out;
        std::nth_element(luma.begin(), luma.begin() + static_cast<long>(luma.size() / 2), luma.end());
        const double median = std::max(1e-6, luma[luma.size() / 2]);

        out.rb = std::log(sumR / sumB);
        out.g = std::log(sumG / std::sqrt(sumR * sumB));
        out.ev = std::log2(median);
        out.ok = true;
        return out;
    }

    Stats Average(const std::vector<Stats>& all)
    {
        Stats avg;
        int n = 0;
        for (const auto& s : all)
        {
            if (!s.ok)
                continue;
            avg.rb += s.rb; avg.g += s.g; avg.ev += s.ev;
            ++n;
        }
        if (n == 0)
            return avg;
        avg.rb /= n; avg.g /= n; avg.ev /= n;
        avg.ok = true;
        return avg;
    }
}
