#include "sdk_diagnostics.h"

#include "EDSDK.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

#include "compatibility_list.inc"

namespace
{
    // "Canon EOS Rebel T7" e "EOS REBEL T7" viram "rebelt7"
    std::string Normalize(const std::string& name)
    {
        std::string lower;
        for (char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)))
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        // Tira "canon", "eos" e "digital" do começo, em qualquer ordem
        bool stripped = true;
        while (stripped)
        {
            stripped = false;
            for (const char* prefix : { "canon", "eos", "digital" })
            {
                const std::string p(prefix);
                if (lower.rfind(p, 0) == 0)
                {
                    lower = lower.substr(p.size());
                    stripped = true;
                }
            }
        }
        return lower;
    }

    // Nomes regionais (Europa/Ásia e Japão) -> nome americano usado na lista da Canon.
    // Fonte: nomenclatura pública da Canon; conferir ao adicionar modelos.
    const std::unordered_map<std::string, std::string>& RegionalAliases()
    {
        static const std::unordered_map<std::string, std::string> aliases = {
            { "850d", "rebelt8i" },   { "kissx10i", "rebelt8i" },
            { "250d", "rebelsl3" },   { "kissx10", "rebelsl3" },  { "200dii", "rebelsl3" }, { "200dmarkii", "rebelsl3" },
            { "2000d", "rebelt7" },   { "kissx90", "rebelt7" },   { "1500d", "rebelt7" },
            { "4000d", "rebelt100" }, { "3000d", "rebelt100" },
            { "200d", "rebelsl2" },   { "kissx9", "rebelsl2" },
            { "800d", "rebelt7i" },   { "kissx9i", "rebelt7i" },
            { "9000d", "77d" },
            { "1300d", "rebelt6" },   { "kissx80", "rebelt6" },
            { "760d", "rebelt6s" },   { "8000d", "rebelt6s" },
            { "750d", "rebelt6i" },   { "kissx8i", "rebelt6i" },
            { "1200d", "rebelt5" },   { "kissx70", "rebelt5" },
            { "100d", "rebelsl1" },   { "kissx7", "rebelsl1" },
            { "700d", "rebelt5i" },   { "kissx7i", "rebelt5i" },
            { "650d", "rebelt4i" },   { "kissx6i", "rebelt4i" },
            { "600d", "rebelt3i" },   { "kissx5", "rebelt3i" },
            { "1100d", "rebelt3" },   { "kissx50", "rebelt3" },
            { "550d", "rebelt2i" },   { "kissx4", "rebelt2i" },
            { "500d", "rebelt1i" },   { "kissx3", "rebelt1i" },
            { "1000d", "rebelxs" },   { "kissf", "rebelxs" },
            { "450d", "rebelxsi" },   { "kissx2", "rebelxsi" },
            { "kissm", "m50" },       { "kissm2", "m50markii" },
        };
        return aliases;
    }
}

bool IsOnCompatibilityList(const std::string& modelName)
{
    std::string key = Normalize(modelName);

    const auto& aliases = RegionalAliases();
    if (auto it = aliases.find(key); it != aliases.end())
        key = it->second;

    for (const char* model : kCompatibleModels)
    {
        if (Normalize(model) == key)
            return true;
    }
    return false;
}

std::string DescribeConnectionError(long error)
{
    switch (error)
    {
    case EDS_ERR_INTERNAL_ERROR:      return "Camera did not respond. Turn it off and on again.";
    case EDS_ERR_DEVICE_BUSY:         return "Camera is busy. Wait a moment and try again.";
    case EDS_ERR_COMM_PORT_IS_IN_USE: return "Camera is being used by another app. Close EOS Utility, Photos or Image Capture.";
    case EDS_ERR_COMM_DISCONNECTED:   return "Camera was disconnected. Check the USB cable.";
    default:                          return "";
    }
}

std::string DescribeShotError(long error)
{
    switch (error)
    {
    case EDS_ERR_TAKE_PICTURE_NO_CARD_NG:      return "no memory card";
    case EDS_ERR_TAKE_PICTURE_CARD_NG:         return "memory card problem (full or damaged)";
    case EDS_ERR_TAKE_PICTURE_CARD_PROTECT_NG: return "memory card is locked";
    case EDS_ERR_DEVICE_BUSY:                  return "camera is busy";
    case EDS_ERR_COMM_DISCONNECTED:            return "camera was disconnected";
    default:                                   return "";
    }
}
