// Configurações de disparo pelas câmeras Canon (EDSDK).
// Valores e nomes conferidos na documentação do EDSDK 13.20.10 (EDSDK API Programming
// Reference, cap. 5.2). Cada câmera informa, por EdsGetPropertyDesc, os valores que
// aceita naquele momento; só esses aparecem como opção.

#include "camera_settings.h"

#include <cstdio>
#include <initializer_list>
#include <map>
#include <set>

namespace
{
    using Table = std::map<long long, const char*>;

    const Table kIso = {
        { 0x00, "ISO Auto" }, { 0x28, "ISO 6" }, { 0x30, "ISO 12" }, { 0x38, "ISO 25" }, { 0x40, "ISO 50" },
        { 0x48, "ISO 100" }, { 0x4b, "ISO 125" }, { 0x4d, "ISO 160" }, { 0x50, "ISO 200" }, { 0x53, "ISO 250" },
        { 0x55, "ISO 320" }, { 0x58, "ISO 400" }, { 0x5b, "ISO 500" }, { 0x5d, "ISO 640" }, { 0x60, "ISO 800" },
        { 0x63, "ISO 1000" }, { 0x65, "ISO 1250" }, { 0x68, "ISO 1600" }, { 0x6b, "ISO 2000" }, { 0x6d, "ISO 2500" },
        { 0x70, "ISO 3200" }, { 0x73, "ISO 4000" }, { 0x75, "ISO 5000" }, { 0x78, "ISO 6400" }, { 0x7b, "ISO 8000" },
        { 0x7d, "ISO 10000" }, { 0x80, "ISO 12800" }, { 0x83, "ISO 16000" }, { 0x85, "ISO 20000" },
        { 0x88, "ISO 25600" }, { 0x8b, "ISO 32000" }, { 0x8d, "ISO 40000" }, { 0x90, "ISO 51200" },
        { 0x93, "ISO 64000" }, { 0x95, "ISO 80000" }, { 0x98, "ISO 102400" }, { 0xa0, "ISO 204800" },
        { 0xa8, "ISO 409600" }, { 0xb0, "ISO 819200" },
    };

    const Table kAv = {
        { 0x08, "f/1" }, { 0x0B, "f/1.1" }, { 0x0C, "f/1.2" }, { 0x0D, "f/1.2" }, { 0x10, "f/1.4" }, { 0x13, "f/1.6" },
        { 0x14, "f/1.8" }, { 0x15, "f/1.8" }, { 0x18, "f/2" }, { 0x1B, "f/2.2" }, { 0x1C, "f/2.5" }, { 0x1D, "f/2.5" },
        { 0x20, "f/2.8" }, { 0x23, "f/3.2" }, { 0x85, "f/3.4" }, { 0x24, "f/3.5" }, { 0x25, "f/3.5" }, { 0x28, "f/4" },
        { 0x2B, "f/4.5" }, { 0x2C, "f/4.5" }, { 0x2D, "f/5" }, { 0x30, "f/5.6" }, { 0x33, "f/6.3" }, { 0x34, "f/6.7" },
        { 0x35, "f/7.1" }, { 0x38, "f/8" }, { 0x3B, "f/9" }, { 0x3C, "f/9.5" }, { 0x3D, "f/10" }, { 0x40, "f/11" },
        { 0x43, "f/13" }, { 0x44, "f/13" }, { 0x45, "f/14" }, { 0x48, "f/16" }, { 0x4B, "f/18" }, { 0x4C, "f/19" },
        { 0x4D, "f/20" }, { 0x50, "f/22" }, { 0x53, "f/25" }, { 0x54, "f/27" }, { 0x55, "f/29" }, { 0x58, "f/32" },
        { 0x5B, "f/36" }, { 0x5C, "f/38" }, { 0x5D, "f/40" }, { 0x60, "f/45" }, { 0x63, "f/51" }, { 0x64, "f/54" },
        { 0x65, "f/57" }, { 0x68, "f/64" }, { 0x6B, "f/72" }, { 0x6C, "f/76" }, { 0x6D, "f/80" }, { 0x70, "f/91" },
    };

    const Table kTv = {
        { 0x0C, "Bulb" }, { 0x10, "30\"" }, { 0x13, "25\"" }, { 0x14, "20\"" }, { 0x15, "20\"" }, { 0x18, "15\"" },
        { 0x1B, "13\"" }, { 0x1C, "10\"" }, { 0x1D, "10\"" }, { 0x20, "8\"" }, { 0x23, "6\"" }, { 0x24, "6\"" },
        { 0x25, "5\"" }, { 0x28, "4\"" }, { 0x2B, "3.2\"" }, { 0x2C, "3\"" }, { 0x2D, "2.5\"" }, { 0x30, "2\"" },
        { 0x33, "1.6\"" }, { 0x34, "1.5\"" }, { 0x35, "1.3\"" }, { 0x38, "1\"" }, { 0x3B, "0.8\"" }, { 0x3C, "0.7\"" },
        { 0x3D, "0.6\"" }, { 0x40, "0.5\"" }, { 0x43, "0.4\"" }, { 0x44, "0.3\"" }, { 0x45, "0.3\"" },
        { 0x48, "1/4" }, { 0x4B, "1/5" }, { 0x4C, "1/6" }, { 0x4D, "1/6" }, { 0x50, "1/8" }, { 0x53, "1/10" },
        { 0x54, "1/10" }, { 0x55, "1/13" }, { 0x58, "1/15" }, { 0x5B, "1/20" }, { 0x5C, "1/20" }, { 0x5D, "1/25" },
        { 0x60, "1/30" }, { 0x63, "1/40" }, { 0x64, "1/45" }, { 0x65, "1/50" }, { 0x68, "1/60" }, { 0x6B, "1/80" },
        { 0x6C, "1/90" }, { 0x6D, "1/100" }, { 0x70, "1/125" }, { 0x73, "1/160" }, { 0x74, "1/180" },
        { 0x75, "1/200" }, { 0x78, "1/250" }, { 0x7B, "1/320" }, { 0x7C, "1/350" }, { 0x7D, "1/400" },
        { 0x80, "1/500" }, { 0x83, "1/640" }, { 0x84, "1/750" }, { 0x85, "1/800" }, { 0x88, "1/1000" },
        { 0x8B, "1/1250" }, { 0x8C, "1/1500" }, { 0x8D, "1/1600" }, { 0x90, "1/2000" }, { 0x93, "1/2500" },
        { 0x94, "1/3000" }, { 0x95, "1/3200" }, { 0x98, "1/4000" }, { 0x9B, "1/5000" }, { 0x9C, "1/6000" },
        { 0x9D, "1/6400" }, { 0xA0, "1/8000" }, { 0xA3, "1/10000" }, { 0xA5, "1/12800" }, { 0xA8, "1/16000" },
        { 0xAB, "1/20000" }, { 0xAD, "1/25600" }, { 0xB0, "1/32000" },
    };

    const Table kExposure = {
        { 0x28, "+5" }, { 0x25, "+4 2/3" }, { 0x24, "+4 1/2" }, { 0x23, "+4 1/3" }, { 0x20, "+4" }, { 0x1D, "+3 2/3" },
        { 0x1C, "+3 1/2" }, { 0x1B, "+3 1/3" }, { 0x18, "+3" }, { 0x15, "+2 2/3" }, { 0x14, "+2 1/2" },
        { 0x13, "+2 1/3" }, { 0x10, "+2" }, { 0x0D, "+1 2/3" }, { 0x0C, "+1 1/2" }, { 0x0B, "+1 1/3" },
        { 0x08, "+1" }, { 0x05, "+2/3" }, { 0x04, "+1/2" }, { 0x03, "+1/3" }, { 0x00, "0" },
        { 0xFD, "-1/3" }, { 0xFC, "-1/2" }, { 0xFB, "-2/3" }, { 0xF8, "-1" }, { 0xF5, "-1 1/3" }, { 0xF4, "-1 1/2" },
        { 0xF3, "-1 2/3" }, { 0xF0, "-2" }, { 0xED, "-2 1/3" }, { 0xEC, "-2 1/2" }, { 0xEB, "-2 2/3" },
        { 0xE8, "-3" }, { 0xE5, "-3 1/3" }, { 0xE4, "-3 1/2" }, { 0xE3, "-3 2/3" }, { 0xE0, "-4" },
        { 0xDD, "-4 1/3" }, { 0xDC, "-4 1/2" }, { 0xDB, "-4 2/3" }, { 0xD8, "-5" },
    };

    const Table kWhiteBalance = {
        { 0, "Auto" }, { 1, "Daylight" }, { 2, "Cloudy" }, { 3, "Tungsten" }, { 4, "Fluorescent" }, { 5, "Flash" },
        { 6, "Custom" }, { 8, "Shade" }, { 9, "Kelvin" }, { 10, "PC-1" }, { 11, "PC-2" }, { 12, "PC-3" },
        { 15, "Custom 2" }, { 16, "Custom 3" }, { 18, "Custom 4" }, { 19, "Custom 5" }, { 20, "PC-4" },
        { 21, "PC-5" }, { 23, "Auto (white)" },
    };

    const Table kPictureStyle = {
        { 0x81, "Standard" }, { 0x82, "Portrait" }, { 0x83, "Landscape" }, { 0x84, "Neutral" }, { 0x85, "Faithful" },
        { 0x86, "Monochrome" }, { 0x87, "Auto" }, { 0x88, "Fine Detail" }, { 0x21, "User 1" }, { 0x22, "User 2" },
        { 0x23, "User 3" }, { 0x41, "PC 1" }, { 0x42, "PC 2" }, { 0x43, "PC 3" },
    };

    const Table kDrive = {
        { 0x00, "Single" }, { 0x01, "Continuous" }, { 0x02, "Video" }, { 0x04, "High-speed cont." },
        { 0x05, "Low-speed cont." }, { 0x06, "Silent single" }, { 0x07, "Self-timer cont." },
        { 0x10, "Self-timer 10 s" }, { 0x11, "Self-timer 2 s" }, { 0x12, "14 fps super high" },
        { 0x13, "Silent single" }, { 0x14, "Silent cont." }, { 0x15, "Silent high-speed" }, { 0x16, "Silent low-speed" },
    };

    const Table kFocus = { { 0, "One-Shot" }, { 1, "AI Servo" }, { 2, "AI Focus" }, { 3, "MF" } };

    const Table kMode = {
        { 0x00, "P" }, { 0x01, "Tv" }, { 0x02, "Av" }, { 0x03, "M" }, { 0x04, "Bulb" }, { 0x05, "A-DEP" },
        { 0x06, "DEP" }, { 0x07, "C" }, { 0x08, "Lock" }, { 0x09, "Auto" }, { 0x0A, "Night Portrait" },
        { 0x0B, "Sports" }, { 0x0C, "Portrait" }, { 0x0D, "Landscape" }, { 0x0E, "Close-up" }, { 0x0F, "Flash Off" },
        { 0x13, "Creative Auto" }, { 0x14, "Movie" }, { 0x15, "Movie" }, { 0x16, "Scene Intelligent Auto" },
        { 0x19, "SCN" }, { 0x37, "Fv" },
    };
    // Modos em que a câmera aceita mudar as configurações de disparo (zona criativa)
    const std::set<long long> kCreativeModes = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x37 };

    const long long kNotValid = 0xffffffffLL;

    std::string Label(const Table& table, long long value)
    {
        auto it = table.find(value);
        if (it != table.end())
            return it->second;
        char text[24];
        std::snprintf(text, sizeof(text), "0x%llx", value);
        return text;
    }

    // Qualidade: bits 24-31 tamanho, 20-23 formato, 16-19 compressão da imagem principal;
    // 8-15, 4-7 e 0-3 os mesmos da segunda imagem (RAW + JPG). Formato/tamanho 0xF/0xFF = nenhuma.
    std::string QualityLabel(long long raw)
    {
        const unsigned value = static_cast<unsigned>(raw);
        auto part = [](unsigned size, unsigned format, unsigned compress) -> std::string
        {
            if (format == 0 || format == 0xF)
                return "";
            if (format == 2 || format == 4 || format == 6)  // CRW, RAW (CR3), CR2
                return compress == 2 ? "C-RAW" : "RAW";
            static const std::map<unsigned, const char*> sizes = {
                { 0, "L" }, { 1, "M" }, { 2, "S" }, { 5, "M1" }, { 6, "M2" }, { 14, "S1" }, { 15, "S2" }, { 16, "S3" } };
            const auto s = sizes.find(size);
            std::string text = s != sizes.end() ? s->second : "?";
            if (format == 8) text += " HEIF";
            if (compress == 5) text += " super fine";
            else if (compress == 3) text += " fine";
            else if (compress == 2) text += " normal";
            return text;
        };
        const std::string main = part((value >> 24) & 0xFF, (value >> 20) & 0xF, (value >> 16) & 0xF);
        const std::string second = ((value >> 8) & 0xFF) == 0xFF ? "" : part((value >> 8) & 0xFF, (value >> 4) & 0xF, value & 0xF);
        if (main.empty())
            return "?";
        return second.empty() ? main : main + " + " + second;
    }

    bool ReadU32(EdsCameraRef camera, EdsPropertyID id, long long& out)
    {
        EdsUInt32 value = 0;
        if (EdsGetPropertyData(camera, id, 0, sizeof(value), &value) != EDS_ERR_OK)
            return false;
        out = value;
        return true;
    }

    bool ReadI32(EdsCameraRef camera, EdsPropertyID id, long long& out)
    {
        EdsInt32 value = 0;
        if (EdsGetPropertyData(camera, id, 0, sizeof(value), &value) != EDS_ERR_OK)
            return false;
        out = value;
        return true;
    }

    std::vector<long long> Allowed(EdsCameraRef camera, EdsPropertyID id, bool isSigned)
    {
        std::vector<long long> values;
        EdsPropertyDesc desc{};
        if (EdsGetPropertyDesc(camera, id, &desc) != EDS_ERR_OK)
            return values;
        for (EdsInt32 i = 0; i < desc.numElements && i < 128; ++i)
        {
            const long long v = isSigned ? static_cast<long long>(desc.propDesc[i])
                                         : static_cast<long long>(static_cast<EdsUInt32>(desc.propDesc[i]));
            if (v != kNotValid)
                values.push_back(v);
        }
        return values;
    }

    CameraSetting Make(EdsCameraRef camera, const char* key, EdsPropertyID id, const Table* table,
                       bool isSigned = false, bool readOnly = false)
    {
        CameraSetting s;
        s.key = key;
        long long value = 0;
        s.available = isSigned ? ReadI32(camera, id, value) : ReadU32(camera, id, value);
        if (!s.available || value == kNotValid)
        {
            s.available = false;
            return s;
        }
        s.value = value;
        auto label = [&](long long v) { return table ? Label(*table, v) : QualityLabel(v); };
        s.label = label(value);
        if (!readOnly)
            for (long long v : Allowed(camera, id, isSigned))
                s.choices.push_back({ v, label(v) });
        return s;
    }

    std::string ErrorText(EdsError err)
    {
        switch (err)
        {
            case EDS_ERR_DEVICE_BUSY: return "The camera is busy. Try again.";
            case EDS_ERR_PROPERTIES_UNAVAILABLE: return "This camera does not have this setting now.";
            case EDS_ERR_NOT_SUPPORTED: return "This camera does not allow this change.";
            default: break;
        }
        char text[64];
        std::snprintf(text, sizeof(text), "The camera did not accept the change (error %lu).", static_cast<unsigned long>(err));
        return text;
    }

    // -----------------------------------------------------------------------
    // Câmeras simuladas (STILLPOINT_FAKE_CAMERAS): valores guardados em memória, para
    // testar a tela Camera settings e o Match colors sem câmera. Cada câmera começa
    // com valores um pouco diferentes, para haver o que igualar.
    // -----------------------------------------------------------------------
    struct FakeState
    {
        long long iso = 0x58, tv = 0x70, av = 0x30, expcomp = 0, wb = 1, style = 0x81, drive = 0;
        int kelvin = 5200, shiftAB = 0, shiftGM = 0, contrast = 0, saturation = 0, colorTone = 0;
    };
    std::map<std::string, FakeState>& FakeStates()
    {
        static std::map<std::string, FakeState> states;
        return states;
    }
    FakeState& FakeFor(const CameraContext& cam)
    {
        auto& states = FakeStates();
        auto it = states.find(cam.shortName);
        if (it == states.end())
        {
            FakeState s;
            const int n = cam.orderPrefix;
            static const long long isos[] = { 0x58, 0x60, 0x58, 0x50, 0x58, 0x68, 0x58, 0x58, 0x60 };
            static const long long tvs[] = { 0x70, 0x70, 0x78, 0x70, 0x70, 0x70, 0x68, 0x70, 0x70 };
            static const long long wbs[] = { 1, 1, 0, 1, 1, 2, 1, 1, 1 };
            static const long long styles[] = { 0x81, 0x81, 0x81, 0x82, 0x81, 0x81, 0x81, 0x84, 0x81 };
            const int k = n > 0 ? (n - 1) % 9 : 0;
            s.iso = isos[k]; s.tv = tvs[k]; s.wb = wbs[k]; s.style = styles[k];
            s.shiftAB = (n % 3 == 0) ? 2 : 0;
            s.shiftGM = (n % 4 == 0) ? -1 : 0;
            s.contrast = (n % 5 == 0) ? 1 : 0;
            s.saturation = (n % 7 == 0) ? -1 : 0;
            it = states.emplace(cam.shortName, s).first;
        }
        return it->second;
    }
    CameraSetting FakeSetting(const std::string& key, long long value, const Table* table,
                              std::initializer_list<long long> choices, bool readOnly = false)
    {
        CameraSetting out;
        out.key = key;
        out.available = true;
        out.value = value;
        out.label = key == "quality" ? QualityLabel(value) : Label(*table, value);
        if (!readOnly)
            for (long long v : choices)
                out.choices.push_back({ v, key == "quality" ? QualityLabel(v) : Label(*table, v) });
        return out;
    }
    CameraSettings FakeRead(const CameraContext& cam)
    {
        const FakeState& f = FakeFor(cam);
        CameraSettings out;
        out.camera = cam.shortName;
        out.model = cam.modelName;
        out.mode = "M";
        out.manualMode = true;
        out.adjustable = true;
        out.settings.push_back(FakeSetting("iso", f.iso, &kIso, { 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80 }));
        out.settings.push_back(FakeSetting("tv", f.tv, &kTv, { 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98 }));
        out.settings.push_back(FakeSetting("av", f.av, &kAv, { 0x20, 0x28, 0x30, 0x38, 0x40, 0x48 }));
        out.settings.push_back(FakeSetting("expcomp", f.expcomp, &kExposure, { 0xF0, 0xF5, 0xF8, 0xFB, 0xFD, 0x00, 0x03, 0x05, 0x08, 0x0B, 0x10 }));
        out.settings.push_back(FakeSetting("wb", f.wb, &kWhiteBalance, { 0, 1, 2, 3, 4, 5, 8, 9 }));
        out.settings.push_back(FakeSetting("style", f.style, &kPictureStyle, { 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87 }));
        out.settings.push_back(FakeSetting("quality", 0x00130000 | 0x0000FF00 | 0x000000F0 | 0x0000000F, nullptr, { }, true));
        out.settings.push_back(FakeSetting("drive", f.drive, &kDrive, { 0x00, 0x01, 0x10, 0x11 }));
        out.settings.push_back(FakeSetting("focus", 3, &kFocus, { }, true));
        out.focusManual = true;
        out.kelvinAvailable = true;
        out.kelvin = f.kelvin;
        out.shiftAvailable = true;
        out.shiftAB = f.shiftAB;
        out.shiftGM = f.shiftGM;
        out.styleDetailAvailable = true;
        out.contrast = f.contrast;
        out.saturation = f.saturation;
        out.colorTone = f.colorTone;
        return out;
    }
    bool FakeWrite(const CameraContext& cam, const std::string& key, long long value, std::string& error)
    {
        FakeState& f = FakeFor(cam);
        auto range = [&](long long lo, long long hi, const char* text) { if (value < lo || value > hi) { error = text; return false; } return true; };
        if (key == "iso") f.iso = value;
        else if (key == "tv") f.tv = value;
        else if (key == "av") f.av = value;
        else if (key == "expcomp") f.expcomp = value;
        else if (key == "wb") f.wb = value;
        else if (key == "style") f.style = value;
        else if (key == "drive") f.drive = value;
        else if (key == "quality") { error = "This camera does not allow this change."; return false; }
        else if (key == "kelvin") { if (!range(2800, 10000, "Use a value from 2800 K to 10000 K.")) return false; f.wb = 9; f.kelvin = static_cast<int>(value / 100 * 100); }
        else if (key == "shiftab") { if (!range(-9, 9, "Use a value from -9 to +9.")) return false; f.shiftAB = static_cast<int>(value); }
        else if (key == "shiftgm") { if (!range(-9, 9, "Use a value from -9 to +9.")) return false; f.shiftGM = static_cast<int>(value); }
        else if (key == "contrast") { if (!range(-4, 4, "Use a value from -4 to +4.")) return false; f.contrast = static_cast<int>(value); }
        else if (key == "saturation") { if (!range(-4, 4, "Use a value from -4 to +4.")) return false; f.saturation = static_cast<int>(value); }
        else if (key == "colortone") { if (!range(-4, 4, "Use a value from -4 to +4.")) return false; f.colorTone = static_cast<int>(value); }
        else { error = "Unknown setting."; return false; }
        return true;
    }
}

namespace CameraSettingsCanon
{
    CameraSettings Read(const CameraContext& cam)
    {
        if (cam.fake)
            return FakeRead(cam);
        CameraSettings out;
        out.camera = cam.shortName;
        out.model = cam.modelName;
        EdsCameraRef camera = cam.camera;
        if (!camera || !cam.sessionOpen)
        {
            out.reason = "The camera is not connected.";
            return out;
        }

        long long mode = kNotValid;
        if (ReadU32(camera, kEdsPropID_AEMode, mode) && mode != kNotValid)
            out.mode = Label(kMode, mode);
        out.manualMode = mode == 0x03;

        out.settings.push_back(Make(camera, "iso", kEdsPropID_ISOSpeed, &kIso));
        out.settings.push_back(Make(camera, "tv", kEdsPropID_Tv, &kTv));
        out.settings.push_back(Make(camera, "av", kEdsPropID_Av, &kAv));
        out.settings.push_back(Make(camera, "expcomp", kEdsPropID_ExposureCompensation, &kExposure));
        out.settings.push_back(Make(camera, "wb", kEdsPropID_WhiteBalance, &kWhiteBalance, true));
        out.settings.push_back(Make(camera, "style", kEdsPropID_PictureStyle, &kPictureStyle));
        out.settings.push_back(Make(camera, "quality", kEdsPropID_ImageQuality, nullptr));
        out.settings.push_back(Make(camera, "drive", kEdsPropID_DriveMode, &kDrive));
        // Foco: só leitura (MF vem da chave da lente; mudar o modo AF só foi testado pela Canon na EOS R5)
        out.settings.push_back(Make(camera, "focus", kEdsPropID_AFMode, &kFocus, false, true));
        const CameraSetting& focus = out.settings.back();
        out.focusManual = focus.available && focus.value == 3;

        long long kelvin = 0;
        if (ReadU32(camera, kEdsPropID_ColorTemperature, kelvin))
        {
            out.kelvinAvailable = true;
            out.kelvin = static_cast<int>(kelvin);
        }

        EdsDataType type = kEdsDataType_Unknown;
        EdsUInt32 size = 0;
        if (EdsGetPropertySize(camera, kEdsPropID_WhiteBalanceShift, 0, &type, &size) == EDS_ERR_OK && size >= 2 * sizeof(EdsInt32))
        {
            std::vector<EdsInt32> shift(size / sizeof(EdsInt32), 0);
            if (EdsGetPropertyData(camera, kEdsPropID_WhiteBalanceShift, 0, size, shift.data()) == EDS_ERR_OK
                && shift[0] != 0x7FFFFFFF)
            {
                out.shiftAvailable = true;
                out.shiftAB = shift[0];
                out.shiftGM = shift[1];
            }
        }

        // Ajustes do Picture Style atual (inParam 0 = o estilo em uso)
        EdsPictureStyleDesc style{};
        if (EdsGetPropertyData(camera, kEdsPropID_PictureStyleDesc, 0, sizeof(style), &style) == EDS_ERR_OK
            && style.contrast >= -4 && style.contrast <= 4 && style.saturation >= -4 && style.saturation <= 4
            && style.colorTone >= -4 && style.colorTone <= 4)
        {
            out.styleDetailAvailable = true;
            out.contrast = style.contrast;
            out.saturation = style.saturation;
            out.colorTone = style.colorTone;
        }

        bool anyChoice = false;
        for (const auto& s : out.settings)
            anyChoice = anyChoice || !s.choices.empty();
        const bool creative = mode == kNotValid || kCreativeModes.count(mode) > 0;

        if (mode == 0x14 || mode == 0x15)
            out.reason = "The camera is in movie mode. Turn the mode dial to M.";
        else if (!creative)
            out.reason = "The mode dial is on " + out.mode + ". Turn it to M on the camera to change the settings here.";
        else if (!anyChoice)
            out.reason = "This camera does not allow changing settings from the computer. Set them on the camera.";
        else if (!out.manualMode && !out.mode.empty())
            out.reason = "The mode dial is on " + out.mode + ": the camera sets some values by itself. Turn it to M for full control.";
        out.adjustable = creative && anyChoice;
        return out;
    }

    bool Write(const CameraContext& cam, const std::string& key, long long value, std::string& error)
    {
        if (cam.fake)
            return FakeWrite(cam, key, value, error);
        EdsCameraRef camera = cam.camera;
        if (!camera || !cam.sessionOpen)
        {
            error = "The camera is not connected.";
            return false;
        }
        auto setU32 = [&](EdsPropertyID id, long long v)
        {
            const EdsUInt32 data = static_cast<EdsUInt32>(v);
            const EdsError err = EdsSetPropertyData(camera, id, 0, sizeof(data), &data);
            if (err != EDS_ERR_OK)
                error = ErrorText(err);
            return err == EDS_ERR_OK;
        };

        static const std::map<std::string, EdsPropertyID> simple = {
            { "iso", kEdsPropID_ISOSpeed }, { "tv", kEdsPropID_Tv }, { "av", kEdsPropID_Av },
            { "expcomp", kEdsPropID_ExposureCompensation }, { "style", kEdsPropID_PictureStyle },
            { "quality", kEdsPropID_ImageQuality }, { "drive", kEdsPropID_DriveMode },
        };
        if (auto it = simple.find(key); it != simple.end())
            return setU32(it->second, value);

        if (key == "wb")
        {
            const EdsInt32 data = static_cast<EdsInt32>(value);
            const EdsError err = EdsSetPropertyData(camera, kEdsPropID_WhiteBalance, 0, sizeof(data), &data);
            if (err != EDS_ERR_OK)
                error = ErrorText(err);
            return err == EDS_ERR_OK;
        }
        if (key == "contrast" || key == "saturation" || key == "colortone")
        {
            if (value < -4 || value > 4)
            {
                error = "Use a value from -4 to +4.";
                return false;
            }
            EdsPictureStyleDesc style{};
            EdsError err = EdsGetPropertyData(camera, kEdsPropID_PictureStyleDesc, 0, sizeof(style), &style);
            if (err == EDS_ERR_OK)
            {
                const EdsInt32 v = static_cast<EdsInt32>(value);
                if (key == "contrast") style.contrast = v;
                else if (key == "saturation") style.saturation = v;
                else style.colorTone = v;
                err = EdsSetPropertyData(camera, kEdsPropID_PictureStyleDesc, 0, sizeof(style), &style);
            }
            if (err != EDS_ERR_OK)
                error = ErrorText(err);
            return err == EDS_ERR_OK;
        }
        if (key == "kelvin")
        {
            if (value < 2800 || value > 10000)
            {
                error = "Use a value from 2800 K to 10000 K.";
                return false;
            }
            const EdsInt32 colorTemp = 9;
            EdsError err = EdsSetPropertyData(camera, kEdsPropID_WhiteBalance, 0, sizeof(colorTemp), &colorTemp);
            if (err == EDS_ERR_OK)
            {
                const EdsUInt32 kelvin = static_cast<EdsUInt32>(value / 100 * 100);
                err = EdsSetPropertyData(camera, kEdsPropID_ColorTemperature, 0, sizeof(kelvin), &kelvin);
            }
            if (err != EDS_ERR_OK)
                error = ErrorText(err);
            return err == EDS_ERR_OK;
        }
        if (key == "shiftab" || key == "shiftgm")
        {
            if (value < -9 || value > 9)
            {
                error = "Use a value from -9 to +9.";
                return false;
            }
            EdsDataType type = kEdsDataType_Unknown;
            EdsUInt32 size = 0;
            EdsError err = EdsGetPropertySize(camera, kEdsPropID_WhiteBalanceShift, 0, &type, &size);
            if (err == EDS_ERR_OK && size >= 2 * sizeof(EdsInt32))
            {
                std::vector<EdsInt32> shift(size / sizeof(EdsInt32), 0);
                err = EdsGetPropertyData(camera, kEdsPropID_WhiteBalanceShift, 0, size, shift.data());
                if (err == EDS_ERR_OK)
                {
                    shift[key == "shiftab" ? 0 : 1] = static_cast<EdsInt32>(value);
                    err = EdsSetPropertyData(camera, kEdsPropID_WhiteBalanceShift, 0, size, shift.data());
                }
            }
            else if (err == EDS_ERR_OK)
                err = EDS_ERR_NOT_SUPPORTED;
            if (err != EDS_ERR_OK)
                error = ErrorText(err);
            return err == EDS_ERR_OK;
        }
        error = "Unknown setting.";
        return false;
    }
}
