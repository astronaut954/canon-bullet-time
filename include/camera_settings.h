#pragma once

// Configurações de disparo de cada câmera (ISO, velocidade, abertura...), para ver e
// igualar todas pelo app. Os dados são iguais para qualquer marca; a leitura e a
// gravação ficam por marca (hoje: Canon, em camera_settings_canon.cpp).
// Chamar só na thread das câmeras e sem conexão em andamento (o SDK fica ocupado).

#include "camera_context.h"

#include <string>
#include <vector>

// Uma opção que a câmera aceita agora (valor do SDK e o texto para mostrar)
struct SettingChoice
{
    long long value = 0;
    std::string label;
};

// Uma configuração: "iso", "tv", "av", "expcomp", "wb", "style", "quality", "drive" ou
// "focus" (só leitura). choices vazio = a câmera não deixa mudar agora.
struct CameraSetting
{
    std::string key;
    bool available = false;   // a câmera tem essa configuração
    long long value = 0;
    std::string label;        // valor atual, para mostrar ("ISO 400", "1/250"...)
    std::vector<SettingChoice> choices;
};

struct CameraSettings
{
    std::string camera;       // nome no app (T7_1...)
    std::string model;
    bool adjustable = false;  // dá para mudar alguma coisa pelo app
    std::string reason;       // por que não dá (ou aviso sobre o modo), em inglês para a tela
    std::string mode;         // modo do disco: "M", "Av", "Tv", "P", "Auto"...
    bool manualMode = false;  // está em M
    int kelvin = 0;           // temperatura de cor (quando o balanço de branco é "Kelvin")
    bool kelvinAvailable = false;
    bool shiftAvailable = false;  // ajuste fino do balanço de branco
    int shiftAB = 0;          // azul (−) ↔ âmbar (+), −9 a +9
    int shiftGM = 0;          // magenta (−) ↔ verde (+), −9 a +9. A documentação do EDSDK diz o
                              // contrário, mas na T7 (2026-10-15) +5 aparece como G5 no menu.
    bool focusManual = false; // lente em MF
    // Ajustes do Picture Style atual (como o INFO da câmera): −4 a +4
    bool styleDetailAvailable = false;
    int contrast = 0;
    int saturation = 0;
    int colorTone = 0;        // − mais vermelho ↔ + mais amarelo (tom de pele)
    std::vector<CameraSetting> settings;
};

namespace CameraSettingsCanon
{
    // Lê tudo de uma câmera conectada.
    CameraSettings Read(const CameraContext& camera);

    // Muda uma configuração. key: as de CameraSetting, mais "kelvin" (valor em K: põe
    // o balanço de branco em temperatura de cor), "shiftab" e "shiftgm" (−9 a +9),
    // "contrast", "saturation" e "colortone" (−4 a +4, no Picture Style atual).
    bool Write(const CameraContext& camera, const std::string& key, long long value, std::string& error);
}
