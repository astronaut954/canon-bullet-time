#include "auto_match.h"

#include "camera_settings.h"
#include "canon_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
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
    // Picture Style: quanto 1 passo de contraste muda a foto (stops entre escuros e claros) e
    // 1 passo de saturação (ln da saturação média). Ponto de partida, conferido em cópias
    // editadas de uma foto; o app corrige pelo que mede depois de mudar.
    constexpr double kStartGainContrast = 0.12;
    constexpr double kStartGainSat = 0.05;
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
            if (cam.shortName == name && cam.sessionOpen && cam.camera != nullptr)
                return &cam;
        return nullptr;
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
        int contrast = 0, saturation = 0;       // ajustes do Picture Style na hora da foto
        std::string style;                      // Picture Style na hora da foto
        double contrastGain = kStartGainContrast, satGain = kStartGainSat;
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
        c.contrast = s.contrast;
        c.saturation = s.saturation;
        const CameraSetting* style = Get(s, "style");
        c.style = style ? std::to_string(style->value) : "";
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
            // Contraste e saturação: quanto 1 passo mudou a medida
            const CameraSetting* style = Get(s, "style");
            if (s.styleDetailAvailable && style && std::to_string(style->value) == c.style)
            {
                if (s.contrast != c.contrast)
                {
                    const double g = (now.contrast - c.last.contrast) / (s.contrast - c.contrast);
                    if (g > 0.02)
                        c.contrastGain = std::clamp(g, 0.03, 0.5);
                }
                if (s.saturation != c.saturation)
                {
                    const double g = (now.sat - c.last.sat) / (s.saturation - c.saturation);
                    if (g > 0.01)
                        c.satGain = std::clamp(g, 0.01, 0.25);
                }
            }
            const std::string key = BrightnessKey(s);
            const CameraSetting* e = Get(s, key);
            double u = 0;
            if (key == c.lightKey && c.lightKnown && e && LightUnits(key, e->value, u) && std::abs(u - c.lightUnits) >= 2)
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
            notes.push_back(c.name + ": its photo could not be measured, so it was not matched.");
            return false;
        }
        const CameraContext* ctx = Find(controller, c.name);
        if (!ctx)
            return false;
        const CameraSettings s = CameraSettingsCanon::Read(*ctx);
        bool wanted = false, changed = false;
        std::string error;

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
                        if (apply && ab != s.shiftAB)
                            changed = CameraSettingsCanon::Write(*ctx, "shiftab", ab, error) || changed;
                        if (apply && gm != s.shiftGM)
                            changed = CameraSettingsCanon::Write(*ctx, "shiftgm", gm, error) || changed;
                    }
                }
            }
        }

        // Contraste e saturação (Picture Style). Mudar o contraste também mexe na saturação
        // medida: com o contraste longe do alvo, a saturação espera a próxima foto.
        if (s.styleDetailAvailable)
        {
            const double tolerance = apply ? ColorMatch::kStepTolerance : kFinalStepTolerance;
            const double conWant = c.contrast + (target.contrast - c.last.contrast) / c.contrastGain;
            const double satWant = c.saturation + (target.sat - c.last.sat) / c.satGain;
            const bool conOff = std::abs(conWant - s.contrast) >= tolerance;
            const bool satOff = std::abs(satWant - s.saturation) >= tolerance;
            auto adjust = [&](const char* key, double want, int now, const char* what)
            {
                const int v = std::clamp(static_cast<int>(std::lround(want)), -4, 4);
                if (std::abs(want) > 4.5)
                    notes.push_back(c.name + ": its " + what + " is more different than the Picture Style can correct.");
                if (v == now)
                    return;
                wanted = true;
                if (apply)
                    changed = CameraSettingsCanon::Write(*ctx, key, v, error) || changed;
            };
            if (conOff)
                adjust("contrast", conWant, s.contrast, "contrast");
            // Saturação junto quando o contraste muda pouco (até 1 passo); senão espera
            if (satOff && std::abs(conWant - s.contrast) < 1.5)
                adjust("saturation", satWant, s.saturation, "saturation");
        }

        // Brilho
        const double wantEv = target.ev - c.last.ev;
        if (std::abs(wantEv) >= (apply ? ColorMatch::kEvTolerance : kFinalEvTolerance))
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
            if (!c.last.ok)
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
            for (const auto& name : wbCams)
                Write(controller, name, "wb", best, notes);
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
               const std::function<bool()>& cancelled)
    {
        Result result;

        // Câmeras que dá para ajustar, na ordem do app
        std::vector<std::pair<int, std::string>> ordered;
        std::vector<std::string> locked;
        std::map<std::string, CameraSettings> before;
        for (const auto& cam : controller.GetCameras())
        {
            if (!cam.sessionOpen || cam.camera == nullptr)
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
        const bool commonWb = !sameFixed && !wbCams.empty();
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
            for (const auto& name : names)
            {
                const CameraContext* ctx = Find(controller, name);
                if (!ctx)
                    continue;
                const CameraSettings& was = before[name];
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
        const Stats target = ColorMatch::Average(all);

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
