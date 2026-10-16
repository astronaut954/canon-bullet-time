#include "color_match.h"

#include "photo_store.h"

#include <algorithm>
#include <cmath>

namespace ColorMatch
{
    namespace
    {
        // sRGB (0–255) para luz linear
        const double* Linear()
        {
            static double table[256];
            static bool ready = false;
            if (!ready)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const double c = i / 255.0;
                    table[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
                }
                ready = true;
            }
            return table;
        }

        // Claridade (L*, 0 a 100) de uma luz linear
        double Lightness(double lum)
        {
            const double f = lum > 0.008856 ? std::cbrt(lum) : 7.787 * lum + 16.0 / 116.0;
            return 116.0 * f - 16.0;
        }

        double EvFromLightness(double meanL)
        {
            const double f = (meanL + 16.0) / 116.0;
            const double lum = f > 0.206893 ? f * f * f : (f - 16.0 / 116.0) / 7.787;
            return std::log2(std::max(1e-6, lum));
        }

        // Só a parte marcada (cartão cinza): cor e brilho médios, sem contraste nem saturação
        Stats MeasureRegion(const RgbFrame& frame, const Region& r)
        {
            Stats out;
            const double* linear = Linear();
            const int x0 = std::clamp(static_cast<int>(r.x0 * frame.width), 0, frame.width - 1);
            const int x1 = std::clamp(static_cast<int>(r.x1 * frame.width), x0 + 1, frame.width);
            const int y0 = std::clamp(static_cast<int>(r.y0 * frame.height), 0, frame.height - 1);
            const int y1 = std::clamp(static_cast<int>(r.y1 * frame.height), y0 + 1, frame.height);
            const int step = std::max(1, std::max(x1 - x0, y1 - y0) / 200);
            double sumR = 0, sumG = 0, sumB = 0, sumL = 0;
            long count = 0, colorCount = 0, nearBlack = 0, blown = 0;
            for (int y = y0; y < y1; y += step)
                for (int x = x0; x < x1; x += step)
                {
                    const unsigned char* p = &frame.pixels[(static_cast<size_t>(y) * frame.width + x) * 3];
                    const double rl = linear[p[0]], gl = linear[p[1]], bl = linear[p[2]];
                    const double lum = 0.2126 * rl + 0.7152 * gl + 0.0722 * bl;
                    sumL += Lightness(lum);
                    ++count;
                    const bool isBlown = p[0] >= 250 || p[1] >= 250 || p[2] >= 250;
                    if (lum < 0.004)
                        ++nearBlack;
                    if (isBlown)
                        ++blown;
                    if (isBlown || lum < 0.004)
                        continue;
                    sumR += rl; sumG += gl; sumB += bl;
                    ++colorCount;
                }
            if (count == 0 || colorCount < count / 2 || sumR <= 0 || sumG <= 0 || sumB <= 0)
                return out;
            out.ev = EvFromLightness(sumL / count);
            out.rb = std::log(sumR / sumB);
            out.g = std::log(sumG / std::sqrt(sumR * sumB));
            out.dark = static_cast<double>(nearBlack) / count;
            out.clipped = static_cast<double>(blown) / count;
            out.exposureOk = out.dark < 0.5 && out.clipped < 0.2;
            out.ok = true;
            return out;
        }

        // Foto dividida em quadradinhos: média e textura de cada um (para achar o cartão)
        struct Cell
        {
            double L = 0, sd = 0, rb = 0, g = 0;
            bool clipped = false, valid = false;
        };
        struct Grid
        {
            int cols = 0, rows = 0;
            std::vector<Cell> cells;
            Cell& at(int c, int r) { return cells[static_cast<size_t>(r) * cols + c]; }
        };

        Grid MakeGrid(const RgbFrame& frame)
        {
            const double* linear = Linear();
            Grid grid;
            const int size = std::max(2, std::max(frame.width, frame.height) / 96);
            grid.cols = frame.width / size;
            grid.rows = frame.height / size;
            grid.cells.resize(static_cast<size_t>(grid.cols) * grid.rows);
            const int step = std::max(1, size / 6);
            for (int r = 0; r < grid.rows; ++r)
                for (int c = 0; c < grid.cols; ++c)
                {
                    double sr = 0, sg = 0, sb = 0, sl = 0, sl2 = 0;
                    int n = 0;
                    bool clipped = false;
                    for (int y = r * size; y < (r + 1) * size; y += step)
                        for (int x = c * size; x < (c + 1) * size; x += step)
                        {
                            const unsigned char* p = &frame.pixels[(static_cast<size_t>(y) * frame.width + x) * 3];
                            const double rl = linear[p[0]], gl = linear[p[1]], bl = linear[p[2]];
                            const double l = Lightness(0.2126 * rl + 0.7152 * gl + 0.0722 * bl);
                            sr += rl; sg += gl; sb += bl; sl += l; sl2 += l * l;
                            clipped = clipped || p[0] >= 250 || p[1] >= 250 || p[2] >= 250;
                            ++n;
                        }
                    Cell& cell = grid.at(c, r);
                    if (n == 0 || sr <= 0 || sg <= 0 || sb <= 0)
                        continue;
                    cell.L = sl / n;
                    cell.sd = std::sqrt(std::max(0.0, sl2 / n - cell.L * cell.L));
                    cell.rb = std::log(sr / sb);
                    cell.g = std::log(sg / std::sqrt(sr * sb));
                    cell.clipped = clipped;
                    cell.valid = true;
                }
            return grid;
        }

        // Quadradinhos parecidos com o primeiro, ligados a ele (lado com lado)
        std::vector<std::pair<int, int>> Grow(Grid& grid, int c0, int r0, std::vector<char>& used, double maxSd,
                                              double maxDL, double maxDColor)
        {
            std::vector<std::pair<int, int>> out;
            const Cell seed = grid.at(c0, r0);
            std::vector<std::pair<int, int>> stack{ { c0, r0 } };
            used[static_cast<size_t>(r0) * grid.cols + c0] = 1;
            while (!stack.empty())
            {
                const auto [c, r] = stack.back();
                stack.pop_back();
                out.push_back({ c, r });
                const int dc[] = { 1, -1, 0, 0 }, dr[] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; ++k)
                {
                    const int nc = c + dc[k], nr = r + dr[k];
                    if (nc < 0 || nr < 0 || nc >= grid.cols || nr >= grid.rows)
                        continue;
                    char& u = used[static_cast<size_t>(nr) * grid.cols + nc];
                    const Cell& n = grid.at(nc, nr);
                    if (u || !n.valid || n.clipped || n.sd > maxSd || std::abs(n.L - seed.L) > maxDL
                        || std::abs(n.rb - seed.rb) > maxDColor || std::abs(n.g - seed.g) > maxDColor * 0.6)
                        continue;
                    u = 1;
                    stack.push_back({ nc, nr });
                }
            }
            return out;
        }

        // Retângulo em volta dos quadradinhos, um quadradinho para dentro (longe da borda do cartão)
        Region BoxOf(const Grid& grid, const std::vector<std::pair<int, int>>& cells)
        {
            int c0 = grid.cols, c1 = -1, r0 = grid.rows, r1 = -1;
            for (const auto& [c, r] : cells)
            {
                c0 = std::min(c0, c); c1 = std::max(c1, c);
                r0 = std::min(r0, r); r1 = std::max(r1, r);
            }
            if (c1 - c0 >= 3) { ++c0; --c1; }
            if (r1 - r0 >= 3) { ++r0; --r1; }
            Region region;
            region.found = true;
            region.x0 = static_cast<double>(c0) / grid.cols;
            region.x1 = static_cast<double>(c1 + 1) / grid.cols;
            region.y0 = static_cast<double>(r0) / grid.rows;
            region.y1 = static_cast<double>(r1 + 1) / grid.rows;
            return region;
        }
    }

    Region FindGrayCard(const std::filesystem::path& photo)
    {
        RgbFrame frame;
        if (!LoadPhotoRgb(photo, frame) || frame.width < 32 || frame.height < 32)
            return {};
        Grid grid = MakeGrid(frame);
        std::vector<char> used(grid.cells.size(), 0);
        double best = 0;
        Region found;
        const double total = static_cast<double>(grid.cols) * grid.rows;
        for (int r = 0; r < grid.rows; ++r)
            for (int c = 0; c < grid.cols; ++c)
            {
                const Cell& cell = grid.at(c, r);
                // Liso, tom médio e cor quase neutra (com folga: com o balanço errado o
                // cinza sai puxado para alguma cor)
                if (used[static_cast<size_t>(r) * grid.cols + c] || !cell.valid || cell.clipped || cell.sd > 2.5
                    || cell.L < 20 || cell.L > 88 || std::abs(cell.rb) > 0.8 || std::abs(cell.g) > 0.3)
                    continue;
                const auto cells = Grow(grid, c, r, used, 2.5, 4.0, 0.08);
                if (cells.size() < 4)
                    continue;
                const Region box = BoxOf(grid, cells);
                const double boxCells = (box.x1 - box.x0) * grid.cols * (box.y1 - box.y0) * grid.rows;
                const double fill = std::min(1.0, cells.size() / std::max(1.0, boxCells));
                const double cx = (box.x0 + box.x1) / 2 - 0.5, cy = (box.y0 + box.y1) / 2 - 0.5;
                const double central = 1.0 - std::min(1.0, std::sqrt(cx * cx + cy * cy) / 0.71) * 0.6;
                const double neutral = 1.0 / (1.0 + 3.0 * (std::abs(cell.rb) + 2.0 * std::abs(cell.g)));
                double score = static_cast<double>(cells.size()) * central * neutral * fill;
                if (cells.size() > total * 0.25)  // parede, chão ou céu: grande demais para ser o cartão
                    score *= 0.15;
                if (score > best)
                {
                    best = score;
                    found = box;
                }
            }
        return found;
    }

    Region GrayCardAt(const std::filesystem::path& photo, double x, double y)
    {
        RgbFrame frame;
        if (!LoadPhotoRgb(photo, frame) || frame.width < 32 || frame.height < 32)
            return {};
        Grid grid = MakeGrid(frame);
        const int c = std::clamp(static_cast<int>(x * grid.cols), 0, grid.cols - 1);
        const int r = std::clamp(static_cast<int>(y * grid.rows), 0, grid.rows - 1);
        const Cell& cell = grid.at(c, r);
        if (cell.valid && !cell.clipped && cell.sd <= 5.0)
        {
            std::vector<char> used(grid.cells.size(), 0);
            const auto cells = Grow(grid, c, r, used, 5.0, 5.0, 0.1);
            if (cells.size() >= 2)
                return BoxOf(grid, cells);
        }
        // Ponto sem área lisa em volta: um quadradinho de 3×3 em volta dele
        Region region;
        region.found = true;
        region.x0 = static_cast<double>(std::max(0, c - 1)) / grid.cols;
        region.x1 = static_cast<double>(std::min(grid.cols, c + 2)) / grid.cols;
        region.y0 = static_cast<double>(std::max(0, r - 1)) / grid.rows;
        region.y1 = static_cast<double>(std::min(grid.rows, r + 2)) / grid.rows;
        return region;
    }

    Stats Measure(const std::filesystem::path& photo, const Region* region)
    {
        Stats out;
        RgbFrame frame;
        if (!LoadPhotoRgb(photo, frame) || frame.width < 8 || frame.height < 8)
            return out;
        if (region)
            return region->found ? MeasureRegion(frame, *region) : out;
        const double* linear = Linear();

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
                    const double lightness = Lightness(lum);  // 0 (preto) a 100 (branco)
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
            out.ev = EvFromLightness(sumL / weights);
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
