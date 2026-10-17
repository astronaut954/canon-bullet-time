#include "auto_match.h"

#include "camera_settings.h"
#include "canon_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

namespace
{
    using AutoMatch::Result;
    using ColorMatch::Stats;

    // Quanto a cor da foto muda por mired de balanço de branco (ln R/B). Conta feita com
    // o corpo negro em sRGB: ~0,010 de 3200 K a 7000 K; um pouco acima por causa do
    // contraste e da saturação da câmera. O app corrige pelo que mede depois de mudar.
    constexpr double kRbPerMired = 0.012;
    // Verde/magenta (ln G/√RB) por mired, quando o balanço de base muda (mesma conta)
    constexpr double kGPerMired = 0.0014;
    // Verde/magenta por passo do ajuste fino (ponto de partida)
    constexpr double kStartGainGM = 0.05;
    // Canon: 1 passo do ajuste fino azul/âmbar ≈ 5 mireds
    constexpr double kMiredsPerStep = 5.0;
    // Na conferência final, diferença que ainda vale avisar (menor que isso não se nota)
    constexpr double kFinalStepTolerance = 1.0;   // passos do ajuste fino
    constexpr double kFinalEvTolerance = 0.3;     // stops

    constexpr long long kStyleAuto = 0x87;
    constexpr long long kStyleStandard = 0x81;
    constexpr long long kWbDaylight = 1;
    constexpr long long kWbKelvin = 9;

    // Predefinidos de balanço de branco com temperatura conhecida (manuais da Canon)
    const std::map<long long, int> kPresetKelvin = {
        { 1, 5200 },  // Daylight
        { 8, 7000 },  // Shade
        { 2, 6000 },  // Cloudy
        { 3, 3200 },  // Tungsten
        { 4, 4000 },  // White fluorescent
    };

    const CameraContext* Find(CanonController& controller, const std::string& name)
    {
        for (const auto& cam : controller.GetCameras())
            if (cam.shortName == name && cam.sessionOpen && (cam.camera != nullptr || cam.fake))
                return &cam;
        return nullptr;
    }

    // Registro do que o ajuste mediu e decidiu (modo debug); quem chama Run define
    std::function<void(const std::string&)> g_trace;
    bool g_grayCard = false;  // medindo o cartão cinza (muda o aviso de foto sem medida)
    void Trace(const std::string& line)
    {
        if (g_trace)
            g_trace(line);
    }
    std::string Num(double v, int decimals = 2)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%+.*f", decimals, v);
        return text;
    }
    std::string MeasureText(const Stats& m)
    {
        if (!m.ok)
            return "not measured";
        return "brightness " + Num(m.ev) + " · color R/B " + Num(m.rb, 3) + " G " + Num(m.g, 3) + " · contrast "
               + Num(m.contrast) + " · saturation " + Num(m.sat, 3) + " · dark " + std::to_string(std::lround(m.dark * 100))
               + "% · clipped " + std::to_string(std::lround(m.clipped * 100)) + "%" + (m.exposureOk ? "" : " (brightness not reliable)");
    }

    const CameraSetting* Get(const CameraSettings& s, const std::string& key)
    {
        for (const auto& c : s.settings)
            if (c.key == key && c.available)
                return &c;
        return nullptr;
    }

    bool Changeable(const CameraSettings& s, const std::string& key)
    {
        const CameraSetting* c = Get(s, key);
        return c && !c->choices.empty();
    }

    bool Accepts(const CameraSetting& c, long long value)
    {
        return std::any_of(c.choices.begin(), c.choices.end(), [&](const SettingChoice& x) { return x.value == value; });
    }

    // Luz na escala da Canon (8 = 1 stop). false: sem valor fixo (ISO Auto, Bulb...)
    bool LightUnits(const std::string& key, long long code, double& units)
    {
        if (key == "iso")
        {
            if (code == 0)
                return false;
            units = static_cast<double>(code);
            return true;
        }
        if (key == "tv")
        {
            if (code < 0x10)
                return false;
            units = static_cast<double>(code);
            return true;
        }
        if (key == "av")
        {
            if (code < 0x08 || code == 0x85)  // 0x85 (f/3.4) fica fora da escala
                return false;
            units = static_cast<double>(code);
            return true;
        }
        if (key == "expcomp")
        {
            if (code < 0 || code > 0xFF)
                return false;
            units = code >= 0x80 ? static_cast<double>(code) - 256.0 : static_cast<double>(code);
            return true;
        }
        return false;
    }

    // Brilho: em M pelo ISO; nos outros modos a câmera compensa o ISO, então pela compensação
    std::string BrightnessKey(const CameraSettings& s)
    {
        return !s.manualMode && Changeable(s, "expcomp") ? "expcomp" : "iso";
    }

    bool IsAutoWb(long long value) { return value == 0 || value == 23; }

    // Mireds (1.000.000 / K) do balanço de branco. false: Auto, Custom ou sem valor conhecido
    bool BaseMired(const CameraSettings& s, double& mired)
    {
        const CameraSetting* wb = Get(s, "wb");
        if (!wb)
            return false;
        if (wb->value == kWbKelvin && s.kelvin > 0)
        {
            mired = 1e6 / s.kelvin;
            return true;
        }
        const auto it = kPresetKelvin.find(wb->value);
        if (it == kPresetKelvin.end())
            return false;
        mired = 1e6 / it->second;
        return true;
    }

    std::string WbId(const CameraSettings& s)
    {
        const CameraSetting* wb = Get(s, "wb");
        if (!wb)
            return "";
        return std::to_string(wb->value) + (wb->value == kWbKelvin ? ":" + std::to_string(s.kelvin) : "");
    }

    std::string ShiftText(int ab, int gm)
    {
        auto part = [](int v, const char* neg, const char* pos)
        { return v == 0 ? std::string("0") : std::string(v < 0 ? neg : pos) + std::to_string(std::abs(v)); };
        return part(ab, "B", "A") + " " + part(gm, "M", "G");
    }

    // O que aparece no resumo, na ordem da tabela
    std::vector<std::pair<std::string, std::string>> SummaryLabels(const CameraSettings& s)
    {
        std::vector<std::pair<std::string, std::string>> out;
        auto add = [&](const char* name, const char* key)
        {
            if (const CameraSetting* c = Get(s, key))
            {
                std::string label = c->label;
                if (label.rfind("ISO ", 0) == 0)
                    label = label.substr(4);
                out.push_back({ name, label });
            }
        };
        add("ISO", "iso");
        add("Shutter", "tv");
        add("Aperture", "av");
        add("Exp. comp.", "expcomp");
        if (const CameraSetting* wb = Get(s, "wb"))
            out.push_back({ "White balance", wb->value == kWbKelvin ? std::to_string(s.kelvin) + " K" : wb->label });
        if (s.shiftAvailable)
            out.push_back({ "WB fine-tune", ShiftText(s.shiftAB, s.shiftGM) });
        add("Picture Style", "style");
        if (s.styleDetailAvailable)
        {
            auto signedText = [](int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); };
            out.push_back({ "Contrast", signedText(s.contrast) });
            out.push_back({ "Saturation", signedText(s.saturation) });
            out.push_back({ "Color tone", signedText(s.colorTone) });
        }
        return out;
    }

    // Deixa a câmera terminar de aplicar o que mudou
    void Settle()
    {
        EdsGetEvent();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EdsGetEvent();
    }

    void Write(CanonController& controller, const std::string& name, const std::string& key, long long value,
               std::vector<std::string>& notes)
    {
        const CameraContext* ctx = Find(controller, name);
        if (!ctx)
            return;
        std::string error;
        if (!CameraSettingsCanon::Write(*ctx, key, value, error))
            notes.push_back(name + ": " + error);
    }

    // Volta as configurações que o app pode ter mudado (cancelar, calibração com a cartela)
    void Restore(CanonController& controller, const std::vector<std::string>& names,
                 const std::map<std::string, CameraSettings>& before)
    {
        for (const auto& name : names)
        {
            const CameraContext* ctx = Find(controller, name);
            if (!ctx)
                continue;
            const auto it = before.find(name);
            if (it == before.end())
                continue;
            const CameraSettings& was = it->second;
            const CameraSettings now = CameraSettingsCanon::Read(*ctx);
            std::string error;
            for (const char* key : { "tv", "av", "iso", "expcomp", "style" })
            {
                const CameraSetting* a = Get(was, key);
                const CameraSetting* b = Get(now, key);
                if (a && b && a->value != b->value && !b->choices.empty())
                    CameraSettingsCanon::Write(*ctx, key, a->value, error);
            }
            const CameraSetting* wbWas = Get(was, "wb");
            if (wbWas && WbId(was) != WbId(now))
            {
                if (wbWas->value == kWbKelvin)
                    CameraSettingsCanon::Write(*ctx, "kelvin", was.kelvin, error);
                else
                    CameraSettingsCanon::Write(*ctx, "wb", wbWas->value, error);
            }
            if (was.styleDetailAvailable && now.styleDetailAvailable)
            {
                if (now.contrast != was.contrast)
                    CameraSettingsCanon::Write(*ctx, "contrast", was.contrast, error);
                if (now.saturation != was.saturation)
                    CameraSettingsCanon::Write(*ctx, "saturation", was.saturation, error);
                if (now.colorTone != was.colorTone)
                    CameraSettingsCanon::Write(*ctx, "colortone", was.colorTone, error);
            }
            if (was.shiftAvailable && now.shiftAB != was.shiftAB)
                CameraSettingsCanon::Write(*ctx, "shiftab", was.shiftAB, error);
            if (was.shiftAvailable && now.shiftGM != was.shiftGM)
                CameraSettingsCanon::Write(*ctx, "shiftgm", was.shiftGM, error);
        }
        Settle();
    }

    // Uma configuração da mesma escala de luz em todas: o valor mais perto da média que
    // todas aceitam (sem valor em comum: o mais perto em cada uma)
    void UnifyLight(CanonController& controller, const std::vector<std::string>& names, const std::string& key,
                    std::vector<std::string>& notes)
    {
        struct Item { std::string name; CameraSetting s; };
        std::vector<Item> items;
        for (const auto& n : names)
            if (const CameraContext* ctx = Find(controller, n))
            {
                const CameraSettings s = CameraSettingsCanon::Read(*ctx);
                if (const CameraSetting* c = Get(s, key); c && !c->choices.empty())
                    items.push_back({ n, *c });
            }
        if (items.size() < 2)
            return;
        double sum = 0;
        int count = 0;
        bool same = true;
        for (const auto& it : items)
        {
            double u = 0;
            if (LightUnits(key, it.s.value, u))
            {
                sum += u;
                ++count;
            }
            same = same && it.s.value == items[0].s.value;
        }
        if (same || count == 0)
            return;
        const double average = sum / count;

        auto nearest = [&](const std::vector<SettingChoice>& choices, bool common) -> std::optional<long long>
        {
            std::optional<long long> best;
            double bestError = 1e9;
            for (const auto& choice : choices)
            {
                double u = 0;
                if (!LightUnits(key, choice.value, u))
                    continue;
                if (common && !std::all_of(items.begin(), items.end(), [&](const Item& it) { return Accepts(it.s, choice.value); }))
                    continue;
                const double error = std::abs(u - average);
                if (error < bestError - 1e-9)
                {
                    bestError = error;
                    best = choice.value;
                }
            }
            return best;
        };
        const std::optional<long long> common = nearest(items[0].s.choices, true);
        for (const auto& it : items)
        {
            const std::optional<long long> want = common ? common : nearest(it.s.choices, false);
            if (want && *want != it.s.value)
                Write(controller, it.name, key, *want, notes);
        }
    }

    // Picture Style: o mais comum entre as câmeras (empate: a primeira na ordem). Nunca
    // Auto, que muda o estilo a cada cena; se só houver Auto, Standard.
    void UnifyStyle(CanonController& controller, const std::vector<std::string>& names, std::vector<std::string>& notes)
    {
        struct Item { std::string name; CameraSetting s; };
        std::vector<Item> items;
        for (const auto& n : names)
            if (const CameraContext* ctx = Find(controller, n))
            {
                const CameraSettings s = CameraSettingsCanon::Read(*ctx);
                if (const CameraSetting* c = Get(s, "style"); c && !c->choices.empty())
                    items.push_back({ n, *c });
            }
        if (items.size() < 2)
            return;
        const bool same = std::all_of(items.begin(), items.end(), [&](const Item& it) { return it.s.value == items[0].s.value; });
        if (same && items[0].s.value != kStyleAuto)
            return;
        std::vector<std::pair<long long, int>> counts;  // na ordem em que aparecem
        for (const auto& it : items)
        {
            if (it.s.value == kStyleAuto)
                continue;
            auto c = std::find_if(counts.begin(), counts.end(), [&](const auto& x) { return x.first == it.s.value; });
            if (c == counts.end())
                counts.push_back({ it.s.value, 1 });
            else
                ++c->second;
        }
        std::stable_sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        counts.push_back({ kStyleStandard, 0 });
        std::optional<long long> chosen;
        for (const auto& c : counts)
            if (std::all_of(items.begin(), items.end(), [&](const Item& it) { return Accepts(it.s, c.first); }))
            {
                chosen = c.first;
                break;
            }
        if (!chosen)
            return;
        for (const auto& it : items)
            if (it.s.value != *chosen)
                Write(controller, it.name, "style", *chosen, notes);
    }

    // Contrast, Saturation e Color tone do Picture Style: o mesmo valor em todas (o mais
    // comum; empate: o da primeira na ordem). Não é medido pela foto: o contraste e a
    // saturação medidos dependem do que cada câmera vê (testado com SL3 e T7: a medida andou
    // ao contrário do ajuste), então o app só garante que todas usem a mesma curva.
    // Com a calibração da cartela, os modelos diferentes do mais comum andam a diferença
    // medida (ex.: T7 com Saturation −1 para ficar como a SL3).
    void UnifyStyleDetails(CanonController& controller, const std::vector<std::string>& names, std::vector<std::string>& notes,
                           const std::map<std::string, AutoMatch::ChartOffsets>& offsets)
    {
        struct Item { std::string name; CameraSettings s; const AutoMatch::ChartOffsets* chart = nullptr; };
        std::vector<Item> items;
        for (const auto& n : names)
            if (const CameraContext* ctx = Find(controller, n))
            {
                CameraSettings s = CameraSettingsCanon::Read(*ctx);
                if (!s.styleDetailAvailable)
                    continue;
                Item it{ n, std::move(s) };
                const CameraSetting* style = Get(it.s, "style");
                if (style)
                    if (const auto found = offsets.find(AutoMatch::ChartKey(it.s.model, style->value)); found != offsets.end())
                        it.chart = &found->second;
                items.push_back(std::move(it));
            }
        if (items.size() < 2)
            return;
        // Com a cartela: o modelo mais comum fica como está; os outros andam a diferença medida
        std::map<std::string, int> modelCount;
        for (const auto& it : items)
            ++modelCount[it.s.model];
        const Item* common = nullptr;
        for (const auto& it : items)
            if (it.chart && (!common || modelCount[it.s.model] > modelCount[common->s.model]))
                common = &it;
        const struct { const char* key; int CameraSettings::*field; double AutoMatch::ChartOffsets::*offset; } details[] = {
            { "contrast", &CameraSettings::contrast, &AutoMatch::ChartOffsets::contrast },
            { "saturation", &CameraSettings::saturation, &AutoMatch::ChartOffsets::saturation },
            { "colortone", &CameraSettings::colorTone, &AutoMatch::ChartOffsets::colorTone },
        };
        for (const auto& d : details)
        {
            // Diferença de cada câmera (em passos inteiros) em relação ao modelo mais comum
            std::vector<int> adjust;
            for (const auto& it : items)
                adjust.push_back(common && it.chart && it.s.model != common->s.model
                                     ? static_cast<int>(std::lround(it.chart->*d.offset - common->chart->*d.offset)) : 0);
            // O valor "como se fossem todas do mesmo modelo" mais comum (empate: o da primeira)
            std::vector<std::pair<int, int>> counts;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int v = items[i].s.*d.field - adjust[i];
                auto c = std::find_if(counts.begin(), counts.end(), [&](const auto& x) { return x.first == v; });
                if (c == counts.end())
                    counts.push_back({ v, 1 });
                else
                    ++c->second;
            }
            std::stable_sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            const int value = counts.front().first;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int want = std::clamp(value + adjust[i], -4, 4);
                if (items[i].s.*d.field == want)
                    continue;
                Trace(items[i].name + ": " + d.key + " " + std::to_string(items[i].s.*d.field) + " -> " + std::to_string(want)
                      + (adjust[i] ? " (same as the others, " + std::string(adjust[i] > 0 ? "+" : "") + std::to_string(adjust[i])
                                         + " for this model, measured with the chart)" : " (same as the others)"));
                Write(controller, items[i].name, d.key, want, notes);
            }
        }
    }

    // Uma câmera durante o ajuste: a última medida e como ela estava na hora da foto
    struct Cam
    {
        std::string name;
        std::string model;
        bool calibrated = false;  // sentido e força já medidos (agora ou antes, guardado)
        Stats last;
        std::string wbId;
        bool baseKnown = false;
        double baseMired = 0;
        int ab = 0, gm = 0;
        std::string lightKey;
        bool lightKnown = false;
        double lightUnits = 0;
        // Quanto cada mudança move a medida (começa no padrão; corrigido pelo que mediu)
        double rbPerMired = kRbPerMired;
        double gainGM = kStartGainGM;
        double evGain = 1.0;
        // Sentido do ajuste fino nesta câmera (1 = o esperado: + âmbar, + verde). A
        // documentação do SDK errou o verde/magenta na T7; então o app confere pela foto e,
        // se a cor andou para o lado contrário, inverte (vale para qualquer modelo).
        int abSign = 1, gmSign = 1;
        bool abKnown = false, gmKnown = false;  // sentido já conferido numa foto
        bool flipped = false;                   // inverteu nesta foto (o app tira uma foto a mais)
        // Mireds "efetivos" na hora da foto: o balanço de base menos o ajuste fino
        double Effective(double base, int ab) const { return base - kMiredsPerStep * abSign * ab; }
    };

    void Remember(Cam& c, const CameraSettings& s)
    {
        c.wbId = WbId(s);
        c.baseKnown = BaseMired(s, c.baseMired);
        if (!c.baseKnown)
            c.baseMired = 0;
        c.ab = s.shiftAB;
        c.gm = s.shiftGM;
        c.lightKey = BrightnessKey(s);
        const CameraSetting* e = Get(s, c.lightKey);
        c.lightKnown = e && LightUnits(c.lightKey, e->value, c.lightUnits);
    }

    // Nova medida: compara com a anterior para saber quanto cada mudança moveu de fato
    void Learn(Cam& c, const Stats& now, const CameraSettings& s)
    {
        if (now.ok && c.last.ok)
        {
            double base = 0;
            const bool known = BaseMired(s, base);
            if (!known)
                base = 0;
            const bool sameBase = WbId(s) == c.wbId;
            // Sentido: só com o mesmo balanço de base e uma mudança clara na foto
            if (sameBase && s.shiftAB != c.ab)
            {
                const double moved = now.rb - c.last.rb;  // + âmbar sobe ln(R/B)
                if (std::abs(moved) > 0.02)
                {
                    if (moved * (s.shiftAB - c.ab) * c.abSign < 0)
                    {
                        c.abSign = -c.abSign;
                        c.flipped = true;
                    }
                    c.abKnown = true;
                }
            }
            if (sameBase && s.shiftGM != c.gm)
            {
                const double moved = now.g - c.last.g;  // + verde sobe ln(G/√RB)
                if (std::abs(moved) > 0.02)
                {
                    if (moved * (s.shiftGM - c.gm) * c.gmSign < 0)
                    {
                        c.gmSign = -c.gmSign;
                        c.flipped = true;
                    }
                    c.gmKnown = true;
                }
            }
            if (sameBase || (known && c.baseKnown))
            {
                const double eNow = c.Effective(base, s.shiftAB);
                const double eLast = c.Effective(c.baseMired, c.ab);
                if (std::abs(eNow - eLast) >= kMiredsPerStep - 0.1)
                {
                    const double k = -(now.rb - c.last.rb) / (eNow - eLast);
                    if (k > 0.003)
                        c.rbPerMired = std::clamp(k, 0.004, 0.03);
                }
            }
            if (sameBase && s.shiftGM != c.gm)
            {
                const double g = c.gmSign * (now.g - c.last.g) / (s.shiftGM - c.gm);
                if (g > 0.01)
                    c.gainGM = std::clamp(g, 0.015, 0.12);
            }
            const bool exposureOk = now.exposureOk && c.last.exposureOk;
            const std::string key = BrightnessKey(s);
            const CameraSetting* e = Get(s, key);
            double u = 0;
            if (exposureOk && key == c.lightKey && c.lightKnown && e && LightUnits(key, e->value, u) && std::abs(u - c.lightUnits) >= 2)
            {
                const double g = (now.ev - c.last.ev) / ((u - c.lightUnits) / 8.0);
                if (g > 0.3)
                    c.evGain = std::clamp(g, 0.5, 2.0);
            }
        }
        c.last = now;
        Remember(c, s);
    }

    // O que falta para a câmera chegar no alvo. apply: muda na câmera. Devolve se falta
    // mudar algo (com apply: se mudou). notes: o que não dá para resolver pelo app.
    bool Correct(CanonController& controller, Cam& c, const Stats& target, bool apply, std::vector<std::string>& notes)
    {
        if (!c.last.ok)
        {
            notes.push_back(c.name + (g_grayCard ? ": the gray card was not marked on its photo, so it was not matched."
                                                 : ": its photo could not be measured, so it was not matched."));
            return false;
        }
        const CameraContext* ctx = Find(controller, c.name);
        if (!ctx)
            return false;
        const CameraSettings s = CameraSettingsCanon::Read(*ctx);
        bool wanted = false, changed = false;
        std::string error;
        std::vector<std::string> why;  // para o registro do modo debug

        // Cor: azul/âmbar pelo balanço (em mireds) e verde/magenta pelo ajuste fino
        double base = 0;
        const bool known = BaseMired(s, base);
        if (!known)
            base = 0;
        const bool sameBase = WbId(s) == c.wbId;
        if (sameBase || (known && c.baseKnown))
        {
            const double eLast = c.Effective(c.baseMired, c.ab);
            const double eWant = eLast - (target.rb - c.last.rb) / c.rbPerMired;
            const double abWant = (base - eWant) / (kMiredsPerStep * c.abSign);
            const double gNow = c.last.g - (sameBase ? 0.0 : kGPerMired * (base - c.baseMired));
            const double gmWant = c.gm + c.gmSign * (target.g - gNow) / c.gainGM;
            const double tolerance = apply ? ColorMatch::kStepTolerance : kFinalStepTolerance;
            const bool abOff = std::abs(abWant - s.shiftAB) >= tolerance;
            const bool gmOff = std::abs(gmWant - s.shiftGM) >= tolerance;
            if (abOff || gmOff)
            {
                if (!s.shiftAvailable)
                    notes.push_back(c.name + ": its color is different, and it has no white balance fine-tune.");
                else
                {
                    // Sentido ainda não conferido: no máximo 3 passos (se for o contrário,
                    // a próxima foto mostra e o estrago é pequeno)
                    auto limit = [](double want, int now, bool known)
                    {
                        int v = static_cast<int>(std::lround(want));
                        if (!known)
                            v = std::clamp(v, now - 3, now + 3);
                        return std::clamp(v, -9, 9);
                    };
                    const int ab = limit(abWant, s.shiftAB, c.abKnown);
                    const int gm = limit(gmWant, s.shiftGM, c.gmKnown);
                    if (std::abs(abWant) > 9.5 || std::abs(gmWant) > 9.5)
                        notes.push_back(c.name + ": its color is more different than the fine-tune can correct.");
                    if (ab != s.shiftAB || gm != s.shiftGM)
                    {
                        wanted = true;
                        why.push_back("fine-tune " + ShiftText(s.shiftAB, s.shiftGM) + " -> " + ShiftText(ab, gm)
                                      + " (wanted " + Num(abWant, 1) + " / " + Num(gmWant, 1) + ")");
                        if (apply && ab != s.shiftAB)
                            changed = CameraSettingsCanon::Write(*ctx, "shiftab", ab, error) || changed;
                        if (apply && gm != s.shiftGM)
                            changed = CameraSettingsCanon::Write(*ctx, "shiftgm", gm, error) || changed;
                    }
                }
            }
        }

        const bool exposureOk = c.last.exposureOk;
        if (!exposureOk)
            notes.push_back(c.name + (c.last.dark >= c.last.clipped ? ": its photo is almost all dark" : ": its photo is mostly blown out")
                            + ", so its brightness was not changed.");

        // Brilho
        const double wantEv = target.ev - c.last.ev;
        if (exposureOk && std::abs(wantEv) >= (apply ? ColorMatch::kEvTolerance : kFinalEvTolerance))
        {
            const std::string key = BrightnessKey(s);
            const CameraSetting* e = Get(s, key);
            double now = 0;
            if (!e || e->choices.empty() || !LightUnits(key, e->value, now))
                notes.push_back(c.name + (key == "iso" ? ": its brightness is different, but its ISO is on Auto. Set an ISO to match it."
                                                       : ": its brightness is different, but it cannot be changed from the app."));
            else
            {
                // O valor que deixa o brilho mais perto do alvo; só muda se chegar bem mais
                // perto (o ISO anda de 1/3 de stop: sem isso, duas câmeras trocam de lugar)
                long long best = e->value;
                double bestError = std::abs(wantEv) - (apply ? 0.05 : 0.15);
                for (const auto& choice : e->choices)
                {
                    double u = 0;
                    if (!LightUnits(key, choice.value, u))
                        continue;
                    const double err = std::abs(wantEv - (u - now) / 8.0 * c.evGain);
                    if (err < bestError - 1e-9)
                    {
                        bestError = err;
                        best = choice.value;
                    }
                }
                if (best != e->value)
                {
                    wanted = true;
                    auto label = [&](long long v)
                    {
                        for (const auto& ch : e->choices)
                            if (ch.value == v)
                                return ch.label;
                        return std::to_string(v);
                    };
                    why.push_back(label(e->value) + " -> " + label(best) + " (brightness " + Num(wantEv) + " stop, gain " + Num(c.evGain) + ")");
                    if (apply)
                        changed = CameraSettingsCanon::Write(*ctx, key, best, error) || changed;
                }
                else if (std::abs(wantEv) > 0.5)
                    notes.push_back(c.name + ": its brightness is different, and its " + (key == "iso" ? "ISO" : "exposure compensation")
                                    + " has no value left in that direction.");
            }
        }
        if (!error.empty())
            notes.push_back(c.name + ": " + error);
        Trace("  " + c.name + ": " + MeasureText(c.last) + (why.empty() ? " -> no change" : " -> " + [&]
        {
            std::string all;
            for (const auto& w : why)
                all += (all.empty() ? "" : "; ") + w;
            return all + (apply ? "" : " (not applied: last photo)");
        }()));
        if (!apply && wanted)
            notes.push_back(c.name + ": still a little different. Take another test shot to refine it.");
        return apply ? changed : wanted;
    }

    // Balanço comum depois da foto em Daylight: o que deixa a cor média perto do alvo
    void ChooseCommonWb(CanonController& controller, const std::vector<Cam>& cams, const std::vector<std::string>& wbCams,
                        const Stats& target, std::vector<std::string>& notes)
    {
        double sum = 0;
        int count = 0;
        bool allKelvin = true;
        std::vector<CameraSetting> lists;
        for (const auto& c : cams)
        {
            if (std::find(wbCams.begin(), wbCams.end(), c.name) == wbCams.end())
                continue;
            const CameraContext* ctx = Find(controller, c.name);
            if (!ctx)
                continue;
            const CameraSettings s = CameraSettingsCanon::Read(*ctx);
            const CameraSetting* wb = Get(s, "wb");
            if (!wb || wb->choices.empty())
                continue;
            lists.push_back(*wb);
            allKelvin = allKelvin && Accepts(*wb, kWbKelvin);
            if (!c.last.ok || !c.baseKnown)
                continue;
            // Mireds que levariam esta câmera ao alvo (mais quente que o alvo: mireds maiores)
            sum += c.Effective(c.baseMired, c.ab) + (c.last.rb - target.rb) / c.rbPerMired;
            ++count;
        }
        if (count == 0 || lists.empty())
            return;
        const double mired = sum / count;
        if (allKelvin)
        {
            const int kelvin = std::clamp(static_cast<int>(std::lround(1e6 / mired / 100.0)) * 100, 2800, 10000);
            for (const auto& name : wbCams)
                Write(controller, name, "kelvin", kelvin, notes);
            Trace("Common white balance: " + std::to_string(kelvin) + " K (estimate " + std::to_string(std::lround(1e6 / mired)) + " K)");
            return;
        }
        long long best = kWbDaylight;
        double bestError = 1e9;
        for (const auto& [value, kelvin] : kPresetKelvin)
        {
            if (!std::all_of(lists.begin(), lists.end(), [&](const CameraSetting& wb) { return Accepts(wb, value); }))
                continue;
            const double error = std::abs(1e6 / kelvin - mired);
            if (error < bestError)
            {
                bestError = error;
                best = value;
            }
        }
        Trace("Common white balance: preset " + std::to_string(best) + " (estimate " + std::to_string(std::lround(1e6 / mired)) + " K)");
        if (best != kWbDaylight)
            for (const auto& name : wbCams)
                Write(controller, name, "wb", best, notes);
    }

    // Brilho alvo da rodada. O ISO (e a compensação) andam de 1/3 de stop: se o alvo fosse
    // sempre a média, duas câmeras a 1/6 de stop de cada lado ficariam paradas (ou
    // trocariam de lugar). Então o alvo é o nível que todas conseguem alcançar mais
    // perto, sem se afastar muito da média.
    double BrightnessTarget(CanonController& controller, const std::vector<Cam>& cams, double average)
    {
        std::vector<std::vector<double>> reach;
        std::vector<double> candidates{ average };
        for (const auto& c : cams)
        {
            if (!c.last.ok || !c.last.exposureOk)
                continue;
            std::vector<double> levels{ c.last.ev };
            if (const CameraContext* ctx = Find(controller, c.name))
            {
                const CameraSettings s = CameraSettingsCanon::Read(*ctx);
                const std::string key = BrightnessKey(s);
                const CameraSetting* e = Get(s, key);
                double now = 0;
                if (e && !e->choices.empty() && LightUnits(key, e->value, now))
                    for (const auto& choice : e->choices)
                    {
                        double u = 0;
                        if (LightUnits(key, choice.value, u) && std::abs(u - now) <= 24)  // até 3 stops
                            levels.push_back(c.last.ev + (u - now) / 8.0 * c.evGain);
                    }
            }
            reach.push_back(levels);
            candidates.push_back(c.last.ev);
        }
        double best = average, bestCost = 1e18;
        for (double t : candidates)
        {
            double cost = 0.5 * (t - average) * (t - average);
            for (const auto& levels : reach)
            {
                double nearest = 1e9;
                for (double l : levels)
                    nearest = (std::min)(nearest, std::abs(l - t));
                cost += nearest * nearest;
            }
            if (cost < bestCost - 1e-12)
            {
                bestCost = cost;
                best = t;
            }
        }
        return best;
    }

    // Depois da primeira foto no balanço comum (já se sabe quanto a cor de cada câmera
    // anda por mired): se outro predefinido deixa o ajuste fino mais perto do zero,
    // troca. Só predefinidos (com Kelvin, o primeiro valor já é o mais perto).
    void RefineCommonWb(CanonController& controller, const std::vector<Cam>& cams, const std::vector<std::string>& wbCams,
                        const Stats& target, std::vector<std::string>& notes)
    {
        double sum = 0, current = 0;
        int count = 0;
        long long currentValue = -1;
        std::vector<CameraSetting> lists;
        for (const auto& c : cams)
        {
            if (std::find(wbCams.begin(), wbCams.end(), c.name) == wbCams.end())
                continue;
            const CameraContext* ctx = Find(controller, c.name);
            if (!ctx)
                continue;
            const CameraSettings s = CameraSettingsCanon::Read(*ctx);
            const CameraSetting* wb = Get(s, "wb");
            if (!wb || wb->choices.empty() || wb->value == kWbKelvin || !kPresetKelvin.count(wb->value))
                return;
            lists.push_back(*wb);
            if (currentValue == -1)
                currentValue = wb->value;
            if (wb->value != currentValue)
                return;
            if (!c.last.ok || !c.baseKnown)
                continue;
            sum += c.Effective(c.baseMired, c.ab) + (c.last.rb - target.rb) / c.rbPerMired;
            ++count;
        }
        if (count == 0 || currentValue == -1)
            return;
        const double mired = sum / count;
        current = 1e6 / kPresetKelvin.at(currentValue);
        long long best = currentValue;
        double bestError = std::abs(current - mired) - 2 * kMiredsPerStep;  // vale trocar só se ganhar 2 passos
        for (const auto& [value, kelvin] : kPresetKelvin)
        {
            if (!std::all_of(lists.begin(), lists.end(), [&](const CameraSetting& wb) { return Accepts(wb, value); }))
                continue;
            const double error = std::abs(1e6 / kelvin - mired);
            if (error < bestError)
            {
                bestError = error;
                best = value;
            }
        }
        if (best != currentValue)
        {
            Trace("Common white balance changed to preset " + std::to_string(best) + " (new estimate " + std::to_string(std::lround(1e6 / mired)) + " K)");
            for (const auto& name : wbCams)
                Write(controller, name, "wb", best, notes);
        }
    }

    // Usa uma calibração (guardada ou medida agora) numa câmera
    void ApplyCalibration(Cam& c, const AutoMatch::Calibration& cal)
    {
        c.abSign = cal.abSign;
        c.gmSign = cal.gmSign;
        c.abKnown = c.gmKnown = c.calibrated = true;
        if (cal.rbPerStep > 0)
            c.rbPerMired = std::clamp(cal.rbPerStep / kMiredsPerStep, 0.004, 0.03);
        if (cal.gPerStep > 0)
            c.gainGM = std::clamp(cal.gPerStep, 0.015, 0.12);
    }

    void SaveCalibration(const std::filesystem::path& file, const std::map<std::string, AutoMatch::Calibration>& all)
    {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::trunc);
        out << "# Stillpoint: ajuste fino do balanço de branco medido por modelo\n"
               "# modelo<TAB>sentido azul/âmbar<TAB>ln(R/B) por passo<TAB>sentido verde/magenta<TAB>ln(G/√RB) por passo\n";
        for (const auto& [model, cal] : all)
            out << model << '\t' << cal.abSign << '\t' << cal.rbPerStep << '\t' << cal.gmSign << '\t' << cal.gPerStep << '\n';
    }
}

namespace AutoMatch
{
    std::map<std::string, Calibration> LoadCalibration(const std::filesystem::path& file)
    {
        std::map<std::string, Calibration> all;
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '\t'))
                parts.push_back(part);
            if (parts.size() < 5 || parts[0].empty())
                continue;
            try
            {
                Calibration cal;
                cal.abSign = std::stoi(parts[1]) < 0 ? -1 : 1;
                cal.rbPerStep = std::stod(parts[2]);
                cal.gmSign = std::stoi(parts[3]) < 0 ? -1 : 1;
                cal.gPerStep = std::stod(parts[4]);
                all[parts[0]] = cal;
            }
            catch (...) {}
        }
        return all;
    }

    Result Run(CanonController& controller, const Shoot& shoot, const std::filesystem::path& calibrationFile,
               const std::function<bool()>& cancelled, const Options& options,
               const std::function<void(const std::string&)>& trace)
    {
        Result result;
        struct TraceGuard { ~TraceGuard() { g_trace = nullptr; g_grayCard = false; } } traceGuard;
        g_trace = trace;
        g_grayCard = options.grayCard;

        // Câmeras que dá para ajustar, na ordem do app
        std::vector<std::pair<int, std::string>> ordered;
        std::vector<std::string> locked;
        std::map<std::string, CameraSettings> before;
        for (const auto& cam : controller.GetCameras())
        {
            if (!cam.sessionOpen || (cam.camera == nullptr && !cam.fake))
                continue;
            CameraSettings s = CameraSettingsCanon::Read(cam);
            if (s.adjustable)
            {
                ordered.push_back({ cam.orderPrefix, cam.shortName });
                before[cam.shortName] = s;
            }
            else
                locked.push_back(cam.shortName);
        }
        std::sort(ordered.begin(), ordered.end());
        std::vector<std::string> names;
        for (const auto& o : ordered)
            names.push_back(o.second);

        std::vector<std::string> notes;
        for (const auto& name : locked)
            notes.push_back(name + ": its settings can only be changed on the camera, so it was not matched.");

        if (names.size() < 2)
        {
            // Sem o que igualar: só a foto de teste
            std::map<std::string, Stats> measured;
            result.ok = shoot(1, 1, "Taking a photo", measured);
            if (!result.ok)
                result.error = "The test photos did not arrive.";
            notes.insert(notes.begin(), "Automatic matching needs at least two cameras whose settings can be changed from the app.");
            result.notes = notes;
            return result;
        }
        result.matched = true;

        // 1. Mesmas configurações em todas (sem foto)
        for (const char* key : { "tv", "av", "iso", "expcomp" })
            UnifyLight(controller, names, key, notes);
        UnifyStyle(controller, names, notes);
        Settle();
        UnifyStyleDetails(controller, names, notes,  // depois do estilo: cada estilo tem os seus
                          options.chartFile.empty() ? std::map<std::string, ChartOffsets>{} : LoadChartCalibration(options.chartFile));
        Settle();

        // Balanço de branco: já igual e fixo em todas? Senão, procura um comum.
        std::vector<std::string> wbCams;
        std::string firstWb;
        bool sameFixed = true;
        for (const auto& name : names)
        {
            const CameraContext* ctx = Find(controller, name);
            if (!ctx)
                continue;
            const CameraSettings s = CameraSettingsCanon::Read(*ctx);
            const CameraSetting* wb = Get(s, "wb");
            if (!wb)
                continue;
            if (!wb->choices.empty())
                wbCams.push_back(name);
            const std::string id = WbId(s);
            if (firstWb.empty())
                firstWb = id;
            sameFixed = sameFixed && id == firstWb && !IsAutoWb(wb->value);
        }
        // Com o cartão cinza o alvo é o cinza neutro, que pode estar longe do balanço atual
        // (o ajuste fino só alcança um pouco): procura o balanço comum mesmo se já é igual
        const bool commonWb = (!sameFixed || options.grayCard) && !wbCams.empty();
        if (!sameFixed && wbCams.empty())
            notes.push_back("The white balance cannot be changed from the app on these cameras. Set the same white balance on each camera.");

        // Calibração guardada por modelo; as câmeras de modelo novo com ajuste fino são
        // calibradas agora (2 fotos), se o balanço de branco ficar fixo (no Auto a câmera
        // muda a cor a cada foto e a medida não vale)
        std::map<std::string, Calibration> calibrations = LoadCalibration(calibrationFile);
        std::vector<Cam> cams;
        bool needCalibration = false;
        for (const auto& name : names)
        {
            Cam c;
            c.name = name;
            c.model = before[name].model;
            if (const auto it = calibrations.find(c.model); it != calibrations.end())
                ApplyCalibration(c, it->second);
            else if (before[name].shiftAvailable && (sameFixed || commonWb))
                needCalibration = true;
            cams.push_back(c);
        }
        int shots = (commonWb ? 2 : 1) + (needCalibration ? 2 : 0) + (commonWb ? 3 : 2);
        bool extraShot = false;  // uma foto a mais quando uma câmera mostrou o sentido contrário
        int shot = 0;

        auto take = [&](const std::string& stage) -> bool
        {
            ++shot;
            std::map<std::string, Stats> measured;
            if (!shoot(shot, shots, stage, measured))
                return false;
            for (auto& c : cams)
            {
                const CameraContext* ctx = Find(controller, c.name);
                if (!ctx)
                {
                    c.last = Stats{};
                    continue;
                }
                const auto it = measured.find(c.name);
                c.flipped = false;
                Learn(c, it != measured.end() ? it->second : Stats{}, CameraSettingsCanon::Read(*ctx));
                if (c.flipped && !extraShot)
                {
                    extraShot = true;
                    ++shots;
                }
                // A foto contradisse a calibração guardada: esquece (calibra de novo na próxima)
                if (c.flipped && calibrations.erase(c.model))
                    SaveCalibration(calibrationFile, calibrations);
            }
            return true;
        };
        auto finish = [&](const std::string& error)
        {
            result.ok = error.empty();
            result.error = error;
            // Resumo: o que mudou em cada câmera
            for (const auto& name : names)
            {
                const CameraContext* ctx = Find(controller, name);
                if (!ctx)
                    continue;
                const auto from = SummaryLabels(before[name]);
                const auto to = SummaryLabels(CameraSettingsCanon::Read(*ctx));
                Change change{ name, {} };
                for (const auto& [setting, value] : to)
                {
                    const auto old = std::find_if(from.begin(), from.end(), [&](const auto& x) { return x.first == setting; });
                    if (old != from.end() && old->second != value)
                        change.items.push_back({ setting, old->second, value });
                }
                if (!change.items.empty())
                    result.changes.push_back(change);
            }
            std::sort(notes.begin(), notes.end());
            notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
            result.notes = notes;
            return result;
        };

        // Cancelado: tudo volta como estava antes do Test shot
        auto stopped = [&]() -> Result
        {
            if (!cancelled())
                return finish("The test photos did not arrive.");
            Restore(controller, names, before);
            Settle();
            result.cancelled = true;
            notes.clear();
            return finish("Cancelled.");
        };

        // 2. Foto como estão: a média de todas é o alvo
        if (!take("Reading how the cameras look"))
            return stopped();
        std::vector<Stats> all;
        for (const auto& c : cams)
            if (c.last.ok)
                all.push_back(c.last);
        if (all.size() < 2)
            return finish("The test photos could not be measured. Check that every camera sent a photo.");
        Stats target = ColorMatch::Average(all);
        if (options.grayCard)
        {
            // Cartão cinza: a cor certa é o cinza sem cor nenhuma; o brilho fica na média
            target.rb = 0;
            target.g = 0;
            Trace("Target (gray card neutral, average brightness): " + MeasureText(target));
        }
        else
            Trace("Target (average of all cameras): " + MeasureText(target));

        // 3. Balanço de branco comum
        if (commonWb)
        {
            for (const auto& name : wbCams)
            {
                Write(controller, name, "wb", kWbDaylight, notes);
                const CameraContext* ctx = Find(controller, name);
                if (ctx && CameraSettingsCanon::Read(*ctx).shiftAvailable)
                {
                    Write(controller, name, "shiftab", 0, notes);
                    Write(controller, name, "shiftgm", 0, notes);
                }
            }
            Settle();
            if (!take("Finding a common white balance"))
                return stopped();
        }

        // 3b. Calibração dos modelos novos: ajuste fino no extremo de um eixo de cada vez
        if (needCalibration)
        {
            struct Probe { std::string name; int ab = 0, gm = 0; Stats base; };
            std::vector<Probe> probes;
            for (const auto& c : cams)
            {
                const CameraContext* ctx = Find(controller, c.name);
                if (c.calibrated || !c.last.ok || !ctx)
                    continue;
                const CameraSettings s = CameraSettingsCanon::Read(*ctx);
                if (s.shiftAvailable && Changeable(s, "wb"))
                    probes.push_back({ c.name, s.shiftAB, s.shiftGM, c.last });
            }
            // Uma foto com o ajuste no lado mais distante; devolve a medida de cada câmera
            auto probeShot = [&](const char* key, bool ab, const std::string& stage, std::map<std::string, Stats>& measured)
            {
                for (const auto& p : probes)
                {
                    const int now = ab ? p.ab : p.gm;
                    Write(controller, p.name, key, now <= 0 ? 9 : -9, notes);
                }
                Settle();
                ++shot;
                const bool ok = shoot(shot, shots, stage, measured);
                for (const auto& p : probes)
                    Write(controller, p.name, key, ab ? p.ab : p.gm, notes);  // volta como estava
                Settle();
                return ok;
            };
            std::map<std::string, Stats> amber, green;
            if (!probeShot("shiftab", true, "Calibrating the cameras (blue/amber)", amber)
                || !probeShot("shiftgm", false, "Calibrating the cameras (green/magenta)", green))
                return stopped();
            bool saved = false;
            for (const auto& p : probes)
            {
                auto a = amber.find(p.name), g = green.find(p.name);
                if (a == amber.end() || g == green.end() || !a->second.ok || !g->second.ok)
                    continue;
                const int dAb = (p.ab <= 0 ? 9 : -9) - p.ab, dGm = (p.gm <= 0 ? 9 : -9) - p.gm;
                const double perAb = (a->second.rb - p.base.rb) / dAb;   // + = como o app mostra (âmbar)
                const double perGm = (g->second.g - p.base.g) / dGm;     // + = como o app mostra (verde)
                auto c = std::find_if(cams.begin(), cams.end(), [&](const Cam& x) { return x.name == p.name; });
                // Mudança pequena demais para confiar (cena mexeu? foto escura?): não guarda
                if (std::abs(perAb) * std::abs(dAb) < 0.06 || std::abs(perGm) * std::abs(dGm) < 0.04)
                {
                    notes.push_back(p.name + ": its calibration photos were not clear enough. It will be calibrated again next time.");
                    continue;
                }
                Calibration cal;
                cal.abSign = perAb < 0 ? -1 : 1;
                cal.gmSign = perGm < 0 ? -1 : 1;
                cal.rbPerStep = std::abs(perAb);
                cal.gPerStep = std::abs(perGm);
                ApplyCalibration(*c, cal);
                Trace("Calibration " + c->model + ": blue/amber " + (cal.abSign > 0 ? "normal" : "inverted") + ", " + Num(cal.rbPerStep, 3)
                      + " per step; green/magenta " + (cal.gmSign > 0 ? "normal" : "inverted") + ", " + Num(cal.gPerStep, 3) + " per step");
                if (!calibrations.count(c->model))
                {
                    calibrations[c->model] = cal;
                    saved = true;
                }
            }
            if (saved)
                SaveCalibration(calibrationFile, calibrations);
        }
        if (commonWb)
        {
            ChooseCommonWb(controller, cams, wbCams, target, notes);
            Settle();
        }

        // 4. Cor e brilho de cada câmera até o alvo; foto de novo para conferir
        std::vector<std::string> state;
        const int baseShot = shot;
        for (;;)
        {
            if (cancelled())
                return stopped();
            const bool last = shot >= shots;
            if (commonWb && !last && shot == baseShot + 1)
                RefineCommonWb(controller, cams, wbCams, target, notes);
            state.clear();
            Stats round = target;
            round.ev = BrightnessTarget(controller, cams, target.ev);
            Trace("After photo " + std::to_string(shot) + " (brightness target " + Num(round.ev) + "):");
            bool any = false;
            for (auto& c : cams)
                any = Correct(controller, c, round, !last, state) || any;
            if (!any || last)
                break;
            Settle();
            if (!take("Matching color and brightness"))
                return stopped();
        }
        notes.insert(notes.end(), state.begin(), state.end());
        return finish("");
    }
}

namespace
{
    void SaveChartCalibration(const std::filesystem::path& file, const std::map<std::string, AutoMatch::ChartOffsets>& all)
    {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::trunc);
        out << "# Stillpoint: diferença de cada modelo medida com a cartela, em passos do Picture Style\n"
               "# modelo|Picture Style<TAB>contrast<TAB>saturation<TAB>color tone\n";
        for (const auto& [key, o] : all)
            out << key << '\t' << o.contrast << '\t' << o.saturation << '\t' << o.colorTone << '\n';
    }

    std::string Signed(double v, int decimals)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%+.*f", decimals, v);
        return text;
    }

    // Resolve J·x = d (3×3); false se J não tem como inverter
    bool Solve3(const double J[3][3], const double d[3], double x[3])
    {
        const double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
                           + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
        if (std::abs(det) < 1e-9)
            return false;
        for (int k = 0; k < 3; ++k)
        {
            double M[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    M[r][c] = c == k ? d[r] : J[r][c];
            x[k] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                    + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
        }
        return true;
    }
}

namespace AutoMatch
{
    std::string ChartKey(const std::string& model, long long style)
    {
        return model + "|" + std::to_string(style);
    }

    std::map<std::string, ChartOffsets> LoadChartCalibration(const std::filesystem::path& file)
    {
        std::map<std::string, ChartOffsets> all;
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '\t'))
                parts.push_back(part);
            if (parts.size() < 4 || parts[0].empty())
                continue;
            try
            {
                ChartOffsets o;
                o.contrast = std::stod(parts[1]);
                o.saturation = std::stod(parts[2]);
                o.colorTone = std::stod(parts[3]);
                all[parts[0]] = o;
            }
            catch (...) {}
        }
        return all;
    }

    ChartResult CalibrateChart(CanonController& controller, const ChartShoot& shoot, const std::filesystem::path& chartFile,
                               const std::function<bool()>& cancelled, const std::function<void(const std::string&)>& trace)
    {
        ChartResult result;
        struct TraceGuard { ~TraceGuard() { g_trace = nullptr; } } traceGuard;
        g_trace = trace;

        // Câmeras que dá para ajustar, com os ajustes do Picture Style, na ordem do app
        std::vector<std::pair<int, std::string>> ordered;
        std::map<std::string, CameraSettings> before;
        std::set<std::string> models;
        for (const auto& cam : controller.GetCameras())
        {
            if (!cam.sessionOpen || (cam.camera == nullptr && !cam.fake))
                continue;
            CameraSettings s = CameraSettingsCanon::Read(cam);
            if (!s.adjustable || !s.styleDetailAvailable)
            {
                result.notes.push_back(cam.shortName + ": its Picture Style cannot be changed from the app, so it was not calibrated.");
                continue;
            }
            ordered.push_back({ cam.orderPrefix, cam.shortName });
            models.insert(s.model);
            before[cam.shortName] = s;
        }
        std::sort(ordered.begin(), ordered.end());
        std::vector<std::string> names;
        for (const auto& o : ordered)
            names.push_back(o.second);
        if (models.size() < 2)
        {
            result.error = "Calibrating with the chart compares camera models, so it needs at least two different models "
                           "connected (for example, an SL3 and a T7). Cameras of the same model already look alike.";
            return result;
        }

        // 1. Mesmo Picture Style, ajustes em 0, mesmo balanço de branco fixo, sem ajuste fino
        std::vector<std::string> notes;
        UnifyStyle(controller, names, notes);
        Settle();
        bool sameFixed = true;
        std::string firstWb;
        for (const auto& name : names)
        {
            const std::string id = WbId(before[name]);
            const CameraSetting* wb = Get(before[name], "wb");
            if (firstWb.empty())
                firstWb = id;
            sameFixed = sameFixed && wb && id == firstWb && !IsAutoWb(wb->value);
        }
        constexpr long long kDaylight = 1;
        for (const auto& name : names)
        {
            const CameraContext* ctx = Find(controller, name);
            if (!ctx)
                continue;
            const CameraSettings s = CameraSettingsCanon::Read(*ctx);
            if (!sameFixed)
                if (const CameraSetting* wb = Get(s, "wb"); wb && !wb->choices.empty() && Accepts(*wb, kDaylight) && wb->value != kDaylight)
                    Write(controller, name, "wb", kDaylight, notes);
            for (const char* key : { "contrast", "saturation", "colortone" })
                Write(controller, name, key, 0, notes);
            if (s.shiftAvailable && s.shiftAB != 0)
                Write(controller, name, "shiftab", 0, notes);
            if (s.shiftAvailable && s.shiftGM != 0)
                Write(controller, name, "shiftgm", 0, notes);
        }
        Settle();
        // Picture Style usado (a calibração vale para ele)
        std::map<std::string, long long> styleOf;
        for (const auto& name : names)
            if (const CameraContext* ctx = Find(controller, name))
            {
                const CameraSettings s = CameraSettingsCanon::Read(*ctx);
                if (const CameraSetting* style = Get(s, "style"))
                    styleOf[name] = style->value;
            }

        // 2. As 4 fotos
        const char* keys[4] = { nullptr, "contrast", "saturation", "colortone" };
        const char* stages[4] = { "Reading the chart", "Contrast +2", "Saturation +2", "Color tone +2" };
        std::map<std::string, std::array<Chart::Reading, 4>> readings;
        for (int k = 0; k < 4; ++k)
        {
            if (cancelled())
                break;
            if (k > 0)
            {
                for (const auto& name : names)
                {
                    if (k > 1)
                        Write(controller, name, keys[k - 1], 0, notes);
                    Write(controller, name, keys[k], 2, notes);
                }
                Settle();
            }
            std::map<std::string, Chart::Reading> measured;
            if (!shoot(k + 1, 4, stages[k], measured))
            {
                Restore(controller, names, before);
                result.cancelled = cancelled();
                result.error = result.cancelled ? "Cancelled." : "The photos did not arrive.";
                return result;
            }
            for (const auto& name : names)
            {
                readings[name][k] = measured.count(name) ? measured[name] : Chart::Reading{};
                const Chart::Reading& r = readings[name][k];
                Trace("Photo " + std::to_string(k + 1) + " (" + stages[k] + ") " + name + ": "
                      + (r.ok ? "contrast " + Num(r.contrast) + " · saturation " + Num(r.sat, 3) + " · tone " + Num(r.tone)
                                    + (r.clipped ? " · clipped" : "")
                              : "not read (" + r.problem + ")"));
            }
        }
        // 3. Tudo volta como estava
        Restore(controller, names, before);
        if (cancelled())
        {
            result.cancelled = true;
            result.error = "Cancelled.";
            return result;
        }

        // 4. Por modelo: como está (base) e quanto cada ajuste mexe (J)
        struct Model { std::string name; int cameras = 0; long long style = 0; double base[3] = {}, plus[3][3] = {}; double J[3][3] = {}; };
        std::vector<Model> list;
        for (const auto& name : names)
        {
            const auto& r = readings[name];
            if (!(r[0].ok && r[1].ok && r[2].ok && r[3].ok))
            {
                std::string why;
                for (const auto& x : r)
                    if (!x.ok)
                    {
                        why = x.problem.empty() ? "the chart was not marked" : x.problem;
                        break;
                    }
                result.notes.push_back(name + ": the chart could not be read (" + why + "), so it was left out.");
                continue;
            }
            if (r[0].clipped)
                result.notes.push_back(name + ": the chart looks too bright or too dark in its photo. For a better result, "
                                              "set the exposure so the chart looks normal and calibrate again.");
            const std::string& model = before[name].model;
            auto it = std::find_if(list.begin(), list.end(), [&](const Model& m) { return m.name == model; });
            if (it == list.end())
            {
                list.push_back(Model{ model });
                it = list.end() - 1;
                it->style = styleOf.count(name) ? styleOf[name] : 0;
            }
            const double v[4][3] = { { r[0].contrast, r[0].sat, r[0].tone }, { r[1].contrast, r[1].sat, r[1].tone },
                                     { r[2].contrast, r[2].sat, r[2].tone }, { r[3].contrast, r[3].sat, r[3].tone } };
            for (int q = 0; q < 3; ++q)
            {
                it->base[q] = (it->base[q] * it->cameras + v[0][q]) / (it->cameras + 1);
                for (int k = 0; k < 3; ++k)
                    it->plus[k][q] = (it->plus[k][q] * it->cameras + v[k + 1][q]) / (it->cameras + 1);
            }
            ++it->cameras;
        }
        // J[q][k]: quanto 1 passo do ajuste k muda a medida q
        std::vector<Model> valid;
        for (auto& m : list)
        {
            for (int k = 0; k < 3; ++k)
                for (int q = 0; q < 3; ++q)
                    m.J[q][k] = (m.plus[k][q] - m.base[q]) / 2.0;
            if (m.J[0][0] < 1.0 || m.J[1][1] < 0.01 || std::abs(m.J[2][2]) < 0.5)
            {
                result.notes.push_back(m.name + ": the Picture Style changes did not show in its photos, so it was not calibrated.");
                continue;
            }
            Trace(m.name + ": contrast " + Num(m.base[0]) + " (" + Num(m.J[0][0]) + " per step) · saturation " + Num(m.base[1], 3)
                  + " (" + Num(m.J[1][1], 3) + " per step) · tone " + Num(m.base[2]) + " (" + Num(m.J[2][2]) + " per step)");
            valid.push_back(m);
        }
        if (valid.size() < 2)
        {
            result.error = "The chart could not be read well enough in at least two camera models. Check that every camera sees "
                           "the whole chart, including the four corner marks, and try again.";
            return result;
        }

        // 5. Diferença de cada modelo em relação ao mais comum, em passos
        const Model* ref = &valid[0];
        for (const auto& m : valid)
            if (m.cameras > ref->cameras)
                ref = &m;
        std::map<std::string, ChartOffsets> found;
        for (const auto& m : valid)
        {
            double x[3] = { 0, 0, 0 };
            if (&m != ref)
            {
                const double d[3] = { ref->base[0] - m.base[0], ref->base[1] - m.base[1], ref->base[2] - m.base[2] };
                if (!Solve3(m.J, d, x))
                    for (int q = 0; q < 3; ++q)
                        x[q] = d[q] / m.J[q][q];
                for (double& v : x)
                    v = std::clamp(v, -8.0, 8.0);
            }
            found[ChartKey(m.name, m.style)] = { x[0], x[1], x[2] };
        }
        // Junta com o que já estava guardado (um modelo já calibrado antes mantém o valor dele)
        std::map<std::string, ChartOffsets> stored = LoadChartCalibration(chartFile);
        ChartOffsets shift;
        int known = 0;
        for (const auto& [key, o] : found)
            if (const auto it = stored.find(key); it != stored.end())
            {
                shift.contrast += it->second.contrast - o.contrast;
                shift.saturation += it->second.saturation - o.saturation;
                shift.colorTone += it->second.colorTone - o.colorTone;
                ++known;
            }
        for (auto& [key, o] : found)
        {
            if (known)
            {
                o.contrast += shift.contrast / known;
                o.saturation += shift.saturation / known;
                o.colorTone += shift.colorTone / known;
            }
            stored[key] = o;
        }
        SaveChartCalibration(chartFile, stored);

        // 6. Resumo para a tela: o que cada modelo vai usar em relação ao mais comum
        const ChartOffsets& r0 = found[ChartKey(ref->name, ref->style)];
        for (const auto& m : valid)
        {
            const ChartOffsets& o = found[ChartKey(m.name, m.style)];
            if (&m == ref)
            {
                result.models.push_back(m.name + "\n   reference (the most common model here)");
                continue;
            }
            const double dc = o.contrast - r0.contrast, ds = o.saturation - r0.saturation, dt = o.colorTone - r0.colorTone;
            auto part = [](const char* what, double v)
            {
                const long step = std::lround(v);
                return std::string(what) + " " + Signed(v, 1) + (step ? " → uses " + std::string(step > 0 ? "+" : "") + std::to_string(step)
                                                                       : " → no change");
            };
            result.models.push_back(m.name + "\n   " + part("Contrast", dc) + "\n   " + part("Saturation", ds) + "\n   "
                                    + part("Color tone", dt));
        }
        result.notes.insert(result.notes.end(), notes.begin(), notes.end());
        std::sort(result.notes.begin(), result.notes.end());
        result.notes.erase(std::unique(result.notes.begin(), result.notes.end()), result.notes.end());
        result.ok = true;
        return result;
    }
}
