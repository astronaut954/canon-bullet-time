#include "chart.h"

#include "photo_store.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    // ---- Medidas da cartela em mm (iguais às de scripts/make_chart.py) ----
    constexpr double kMark = 22.0;
    // Centros das marcas, na ordem de Chart::Quad (em cima à esquerda, em cima à direita,
    // embaixo à direita, embaixo à esquerda)
    constexpr double kMarkX[4] = { 20 + kMark / 2, 255 + kMark / 2, 255 + kMark / 2, 20 + kMark / 2 };
    constexpr double kMarkY[4] = { 20 + kMark / 2, 20 + kMark / 2, 156 + kMark / 2, 156 + kMark / 2 };
    constexpr double kDotX = 48.0, kDotY = 23.0, kDotR = 3.0;
    constexpr double kGX0 = 56.0, kGX1 = 241.0, kGap = 4.0;
    constexpr double kWedgeY0 = 24.0, kWedgeY1 = 80.0;
    constexpr int kWedge = 11;
    constexpr double kGridY0 = 90.0, kGridY1 = 174.0;
    constexpr int kColors = 16;

    struct Rect { double x0, y0, x1, y1; };

    Rect WedgePatch(int i)
    {
        const double w = (kGX1 - kGX0 - kGap * (kWedge - 1)) / kWedge;
        const double x = kGX0 + i * (w + kGap);
        return { x, kWedgeY0, x + w, kWedgeY1 };
    }
    Rect ColorPatch(int k)
    {
        const double cw = (kGX1 - kGX0 - kGap * 9) / 10;
        const double ch = (kGridY1 - kGridY0 - kGap) / 2;
        const int col = (k % 4) + (k < 8 ? 0 : 6);
        const int row = (k % 8) / 4;
        const double x = kGX0 + col * (cw + kGap), y = kGridY0 + row * (ch + kGap);
        return { x, y, x + cw, y + ch };
    }
    Rect MidGray()
    {
        const double cw = (kGX1 - kGX0 - kGap * 9) / 10;
        const double x = kGX0 + 4 * (cw + kGap);
        return { x, kGridY0, x + 2 * cw + kGap, kGridY1 };
    }

    // ---- Homografia: da cartela (mm) para a foto (pixels) ----
    struct Homography
    {
        double h[9] = {};
        bool ok = false;
        void Map(double x, double y, double& u, double& v) const
        {
            const double w = h[6] * x + h[7] * y + h[8];
            u = (h[0] * x + h[1] * y + h[2]) / w;
            v = (h[3] * x + h[4] * y + h[5]) / w;
        }
    };

    Homography Solve(const double* sx, const double* sy, const double* dx, const double* dy)
    {
        // 8 equações, 8 incógnitas (h8 = 1), eliminação de Gauss com pivô
        double a[8][9] = {};
        for (int i = 0; i < 4; ++i)
        {
            double* r1 = a[2 * i];
            double* r2 = a[2 * i + 1];
            r1[0] = sx[i]; r1[1] = sy[i]; r1[2] = 1; r1[6] = -sx[i] * dx[i]; r1[7] = -sy[i] * dx[i]; r1[8] = dx[i];
            r2[3] = sx[i]; r2[4] = sy[i]; r2[5] = 1; r2[6] = -sx[i] * dy[i]; r2[7] = -sy[i] * dy[i]; r2[8] = dy[i];
        }
        Homography out;
        for (int c = 0; c < 8; ++c)
        {
            int p = c;
            for (int r = c + 1; r < 8; ++r)
                if (std::abs(a[r][c]) > std::abs(a[p][c]))
                    p = r;
            if (std::abs(a[p][c]) < 1e-12)
                return out;
            std::swap(a[p], a[c]);
            for (int r = 0; r < 8; ++r)
            {
                if (r == c)
                    continue;
                const double f = a[r][c] / a[c][c];
                for (int k = c; k < 9; ++k)
                    a[r][k] -= f * a[c][k];
            }
        }
        for (int i = 0; i < 8; ++i)
            out.h[i] = a[i][8] / a[i][i];
        out.h[8] = 1;
        out.ok = true;
        return out;
    }

    // ---- Foto em tons de cinza, reduzida (para achar as marcas) ----
    struct Gray
    {
        int w = 0, h = 0;
        std::vector<unsigned char> v;
        unsigned char at(int x, int y) const { return v[static_cast<size_t>(y) * w + x]; }
    };

    Gray Reduce(const RgbFrame& frame, int maxSide)
    {
        Gray g;
        const int factor = std::max(1, (std::max(frame.width, frame.height) + maxSide - 1) / maxSide);
        g.w = frame.width / factor;
        g.h = frame.height / factor;
        g.v.resize(static_cast<size_t>(g.w) * g.h);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
            {
                int sum = 0;
                for (int dy = 0; dy < factor; ++dy)
                    for (int dx = 0; dx < factor; ++dx)
                    {
                        const unsigned char* p = &frame.pixels[(static_cast<size_t>(y * factor + dy) * frame.width + x * factor + dx) * 3];
                        sum += (p[0] * 54 + p[1] * 183 + p[2] * 19) >> 8;
                    }
                g.v[static_cast<size_t>(y) * g.w + x] = static_cast<unsigned char>(sum / (factor * factor));
            }
        return g;
    }

    // Escuro / claro pela média em volta (luz desigual na cartela não atrapalha)
    std::vector<unsigned char> Binarize(const Gray& g)
    {
        std::vector<long long> sum(static_cast<size_t>(g.w + 1) * (g.h + 1), 0);
        for (int y = 0; y < g.h; ++y)
        {
            long long row = 0;
            for (int x = 0; x < g.w; ++x)
            {
                row += g.at(x, y);
                sum[static_cast<size_t>(y + 1) * (g.w + 1) + x + 1] = sum[static_cast<size_t>(y) * (g.w + 1) + x + 1] + row;
            }
        }
        const int r = std::max(8, std::max(g.w, g.h) / 24);
        std::vector<unsigned char> dark(g.v.size());
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
            {
                const int x0 = std::max(0, x - r), x1 = std::min(g.w, x + r + 1);
                const int y0 = std::max(0, y - r), y1 = std::min(g.h, y + r + 1);
                const long long s = sum[static_cast<size_t>(y1) * (g.w + 1) + x1] - sum[static_cast<size_t>(y0) * (g.w + 1) + x1]
                                    - sum[static_cast<size_t>(y1) * (g.w + 1) + x0] + sum[static_cast<size_t>(y0) * (g.w + 1) + x0];
                const double mean = static_cast<double>(s) / ((x1 - x0) * (y1 - y0));
                dark[static_cast<size_t>(y) * g.w + x] = g.at(x, y) < mean - 10 ? 1 : 0;
            }
        return dark;
    }

    // Cinco faixas escuro, claro, escuro, claro, escuro na proporção 1:1:2:1:1 (o meio da marca)
    bool Ratio(const int* w, double& module)
    {
        const int total = w[0] + w[1] + w[2] + w[3] + w[4];
        if (total < 10)
            return false;
        module = total / 6.0;
        const double m = module;
        return std::abs(w[0] - m) < 0.6 * m && std::abs(w[1] - m) < 0.6 * m && std::abs(w[2] - 2 * m) < 1.0 * m
               && std::abs(w[3] - m) < 0.6 * m && std::abs(w[4] - m) < 0.6 * m;
    }

    // Confere na vertical, a partir do ponto (no meio escuro da marca)
    bool Vertical(const std::vector<unsigned char>& dark, int w, int h, int x, int y, double& cy, double& module)
    {
        auto d = [&](int yy) { return dark[static_cast<size_t>(yy) * w + x] != 0; };
        if (!d(y))
            return false;
        int runs[5] = {};
        int up = y;
        while (up >= 0 && d(up)) { ++runs[2]; --up; }
        while (up >= 0 && !d(up)) { ++runs[1]; --up; }
        while (up >= 0 && d(up)) { ++runs[0]; --up; }
        int down = y + 1;
        while (down < h && d(down)) { ++runs[2]; ++down; }
        while (down < h && !d(down)) { ++runs[3]; ++down; }
        while (down < h && d(down)) { ++runs[4]; ++down; }
        if (!Ratio(runs, module))
            return false;
        const int centerTop = up + runs[0] + runs[1] + 1;
        cy = centerTop + runs[2] / 2.0;
        return true;
    }

    struct Candidate { double x, y, module; int hits; };

    std::vector<Candidate> FindMarks(const Gray& g)
    {
        const std::vector<unsigned char> dark = Binarize(g);
        std::vector<Candidate> found;
        for (int y = 0; y < g.h; ++y)
        {
            // Faixas da linha
            std::vector<std::pair<int, int>> runs;  // (início, largura), começando pela cor do pixel 0
            std::vector<bool> isDark;
            int start = 0;
            for (int x = 1; x <= g.w; ++x)
                if (x == g.w || dark[static_cast<size_t>(y) * g.w + x] != dark[static_cast<size_t>(y) * g.w + x - 1])
                {
                    runs.push_back({ start, x - start });
                    isDark.push_back(dark[static_cast<size_t>(y) * g.w + start] != 0);
                    start = x;
                }
            for (size_t i = 0; i + 4 < runs.size(); ++i)
            {
                if (!isDark[i])
                    continue;
                const int w[5] = { runs[i].second, runs[i + 1].second, runs[i + 2].second, runs[i + 3].second, runs[i + 4].second };
                double module = 0;
                if (!Ratio(w, module))
                    continue;
                const int cx = runs[i + 2].first + runs[i + 2].second / 2;
                double cy = 0, vModule = 0;
                if (!Vertical(dark, g.w, g.h, cx, y, cy, vModule))
                    continue;
                // Proporção parecida na horizontal e na vertical (a cartela pode estar inclinada)
                if (vModule > module * 3 || module > vModule * 3)
                    continue;
                const double m = (module + vModule) / 2;
                bool merged = false;
                for (auto& c : found)
                    if (std::hypot(c.x - cx, c.y - cy) < 2.5 * std::max(c.module, m))
                    {
                        c.x = (c.x * c.hits + cx) / (c.hits + 1);
                        c.y = (c.y * c.hits + cy) / (c.hits + 1);
                        c.module = (c.module * c.hits + m) / (c.hits + 1);
                        ++c.hits;
                        merged = true;
                        break;
                    }
                if (!merged)
                    found.push_back({ static_cast<double>(cx), cy, m, 1 });
            }
        }
        // Uma marca de verdade aparece em várias linhas
        std::vector<Candidate> out;
        for (const auto& c : found)
            if (c.hits >= std::max(2.0, c.module * 0.8))
                out.push_back(c);
        std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) { return a.hits > b.hits; });
        if (out.size() > 10)
            out.resize(10);
        return out;
    }

    double MeanGray(const Gray& g, const Homography& H, double cx, double cy, double radius)
    {
        double sum = 0;
        int n = 0;
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j)
            {
                double u = 0, v = 0;
                H.Map(cx + radius * i / 2.0, cy + radius * j / 2.0, u, v);
                const int x = static_cast<int>(u), y = static_cast<int>(v);
                if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                    return -1;
                sum += g.at(x, y);
                ++n;
            }
        return sum / n;
    }

    // ---- Cor ----
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

    double LabF(double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; }

    struct Patch { double r = 0, g = 0, b = 0, maxValue = 0; bool ok = false; };

    // Média da parte de dentro (60%) de uma amostra, em luz linear
    Patch Sample(const RgbFrame& frame, const Homography& H, const Rect& rect)
    {
        const double* linear = Linear();
        Patch p;
        const double mx = (rect.x1 - rect.x0) * 0.2, my = (rect.y1 - rect.y0) * 0.2;
        int n = 0;
        double maxSum = 0;
        constexpr int kSteps = 10;
        for (int i = 0; i < kSteps; ++i)
            for (int j = 0; j < kSteps; ++j)
            {
                const double x = rect.x0 + mx + (rect.x1 - rect.x0 - 2 * mx) * (i + 0.5) / kSteps;
                const double y = rect.y0 + my + (rect.y1 - rect.y0 - 2 * my) * (j + 0.5) / kSteps;
                double u = 0, v = 0;
                H.Map(x, y, u, v);
                const int px = static_cast<int>(u), py = static_cast<int>(v);
                if (px < 0 || py < 0 || px >= frame.width || py >= frame.height)
                    return p;
                const unsigned char* c = &frame.pixels[(static_cast<size_t>(py) * frame.width + px) * 3];
                p.r += linear[c[0]];
                p.g += linear[c[1]];
                p.b += linear[c[2]];
                maxSum += std::max({ c[0], c[1], c[2] });
                ++n;
            }
        p.r /= n; p.g /= n; p.b /= n;
        p.maxValue = maxSum / n;
        p.ok = true;
        return p;
    }
}

namespace Chart
{
    Quad Find(const std::filesystem::path& photo)
    {
        Quad out;
        RgbFrame frame;
        if (!LoadPhotoRgb(photo, frame) || frame.width < 64 || frame.height < 64)
            return out;
        const Gray g = Reduce(frame, 2000);
        const std::vector<Candidate> marks = FindMarks(g);
        if (marks.size() < 4)
            return out;

        double best = 0;
        const int n = static_cast<int>(marks.size());
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                for (int c = b + 1; c < n; ++c)
                    for (int d = c + 1; d < n; ++d)
                    {
                        std::array<Candidate, 4> pick{ marks[a], marks[b], marks[c], marks[d] };
                        // Marcas de tamanhos parecidos
                        double mn = 1e9, mx = 0;
                        for (const auto& p : pick) { mn = std::min(mn, p.module); mx = std::max(mx, p.module); }
                        if (mx > mn * 3)
                            continue;
                        // Em volta do centro, no sentido do relógio (na foto, y para baixo)
                        double cx = 0, cy = 0;
                        for (const auto& p : pick) { cx += p.x / 4; cy += p.y / 4; }
                        std::sort(pick.begin(), pick.end(), [&](const Candidate& p, const Candidate& q)
                                  { return std::atan2(p.y - cy, p.x - cx) < std::atan2(q.y - cy, q.x - cx); });
                        // As 4 voltas possíveis: a certa tem a bolinha escura e o cinza do claro ao escuro
                        for (int rot = 0; rot < 4; ++rot)
                        {
                            double dx[4], dy[4];
                            for (int k = 0; k < 4; ++k)
                            {
                                dx[k] = pick[(k + rot) % 4].x;
                                dy[k] = pick[(k + rot) % 4].y;
                            }
                            const Homography H = Solve(kMarkX, kMarkY, dx, dy);
                            if (!H.ok)
                                continue;
                            const double dot = MeanGray(g, H, kDotX, kDotY, kDotR * 0.5);
                            const double paper = MeanGray(g, H, kDotX, kDotY + 9, 1.5);
                            const Rect w0 = WedgePatch(0), w10 = WedgePatch(kWedge - 1);
                            const double light = MeanGray(g, H, (w0.x0 + w0.x1) / 2, (w0.y0 + w0.y1) / 2, 3);
                            const double darkPatch = MeanGray(g, H, (w10.x0 + w10.x1) / 2, (w10.y0 + w10.y1) / 2, 3);
                            if (dot < 0 || paper < 0 || light < 0 || darkPatch < 0)
                                continue;
                            const double dotContrast = paper - dot, wedge = light - darkPatch;
                            if (dotContrast < 25 || wedge < 25)
                                continue;
                            const double score = dotContrast + wedge + (pick[0].hits + pick[1].hits + pick[2].hits + pick[3].hits) * 0.1;
                            if (score > best)
                            {
                                best = score;
                                out.found = true;
                                for (int k = 0; k < 4; ++k)
                                {
                                    out.x[k] = dx[k] / g.w;
                                    out.y[k] = dy[k] / g.h;
                                }
                            }
                        }
                    }
        return out;
    }

    Reading Measure(const std::filesystem::path& photo, const Quad& quad)
    {
        Reading out;
        RgbFrame frame;
        if (!LoadPhotoRgb(photo, frame) || frame.width < 64 || frame.height < 64)
        {
            out.problem = "the photo could not be read";
            return out;
        }
        double dx[4], dy[4];
        for (int k = 0; k < 4; ++k)
        {
            dx[k] = quad.x[k] * frame.width;
            dy[k] = quad.y[k] * frame.height;
        }
        const Homography H = Solve(kMarkX, kMarkY, dx, dy);
        if (!H.ok)
        {
            out.problem = "the chart corners are not marked right";
            return out;
        }
        // Balanço de branco tirado pelo cinza médio: as câmeras são comparadas sem a
        // diferença de cor da luz (essa fica com o balanço de branco e o ajuste fino)
        const Patch gray = Sample(frame, H, MidGray());
        if (!gray.ok || gray.r <= 0 || gray.g <= 0 || gray.b <= 0)
        {
            out.problem = "the chart is not whole in the photo";
            return out;
        }
        const double kr = gray.g / gray.r, kb = gray.g / gray.b;
        auto lab = [&](const Patch& p, double& L, double& a, double& b)
        {
            const double r = p.r * kr, g = p.g, bl = p.b * kb;
            const double X = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.9505;
            const double Y = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
            const double Z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.089;
            const double fx = LabF(X), fy = LabF(Y), fz = LabF(Z);
            L = 116 * fy - 16;
            a = 500 * (fx - fy);
            b = 200 * (fy - fz);
        };
        for (int i = 0; i < kWedge; ++i)
        {
            const Patch p = Sample(frame, H, WedgePatch(i));
            if (!p.ok)
            {
                out.problem = "the chart is not whole in the photo";
                return out;
            }
            double a = 0, b = 0;
            lab(p, out.wedge[i], a, b);
            // Os tons usados no contraste (3º e 9º) não podem estar estourados nem sem detalhe
            if (i == 2 && p.maxValue >= 250)
                out.clipped = true;
        }
        if (out.wedge[8] < 5)
            out.clipped = true;
        if (out.wedge[0] - out.wedge[kWedge - 1] < 20)
        {
            out.problem = "the gray steps are not readable (wrong corners, or the chart is too dark or too bright)";
            return out;
        }
        // Contraste: do 3º tom (claro) ao 9º (escuro), longe das pontas que estouram ou somem
        out.contrast = out.wedge[2] - out.wedge[8];

        double chroma = 0, skinX = 0, skinY = 0;
        for (int k = 0; k < kColors; ++k)
        {
            const Patch p = Sample(frame, H, ColorPatch(k));
            if (!p.ok)
            {
                out.problem = "the chart is not whole in the photo";
                return out;
            }
            double L = 0, a = 0, b = 0;
            lab(p, L, a, b);
            chroma += std::hypot(a, b);
            if (k < 2)  // tons de pele
            {
                const double c = std::max(1e-6, std::hypot(a, b));
                skinX += a / c;
                skinY += b / c;
            }
        }
        out.sat = std::log(std::max(1e-3, chroma / kColors));
        out.tone = std::atan2(skinY, skinX) * 180.0 / 3.14159265358979;
        out.ok = true;
        return out;
    }
}
