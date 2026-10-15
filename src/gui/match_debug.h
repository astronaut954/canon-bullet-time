#pragma once

// Modo debug do Test shot (ligado em Settings). Cada Test shot ganha uma pasta em
// <dados do app>/Debug/<data-hora> com:
//  - as prévias que o app mediu (photo-<n>_<câmera>.jpg; em RAW, a prévia JPEG embutida);
//  - report.csv: por foto e câmera, as configurações usadas e o que foi medido;
//  - trace.txt: o que o "Match automatically" mediu e decidiu, passo a passo.
// Serve para mandar a pasta e analisar com os números reais. Guarda as 20 mais recentes.

#include "camera_settings.h"
#include "color_match.h"

#include <filesystem>
#include <string>
#include <vector>

class MatchDebug
{
public:
    struct Photo
    {
        std::string camera;
        CameraSettings settings;          // como a câmera estava nessa foto
        std::filesystem::path file;       // a foto que chegou (JPEG ou RAW)
        ColorMatch::Stats measured;
    };

    // kind: "test" (Test shot simples) ou "match" (com Match automatically)
    bool Begin(const std::filesystem::path& root, const std::string& kind);
    void Shot(int shot, const std::string& stage, const std::vector<Photo>& photos);
    void Line(const std::string& text);

    bool Active() const { return !folder.empty(); }
    const std::filesystem::path& Folder() const { return folder; }

private:
    std::filesystem::path folder;
};
