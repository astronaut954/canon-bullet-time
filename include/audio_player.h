#pragma once

#include <string>

namespace AudioPlayer
{
    // Um som por vez: o novo interrompe o anterior (versão de terminal).
    bool PlayWavAsync(const std::string& filePath);
    bool PlayWavSync(const std::string& filePath);
    void Stop();

    // Sons sobrepostos, sem interromper o anterior e sem atraso para começar (o arquivo
    // fica carregado na memória depois da primeira vez). Usado pela interface gráfica
    // (contagem entre grupos e som dos grupos da janela Live). Pode ser chamado de
    // qualquer thread.
    // duckOthers: enquanto este som toca, os que já estavam tocando ficam mais baixos
    // (ducking) e voltam ao normal quando ele termina.
    bool PlayWavMixed(const std::string& filePath, bool duckOthers = false);
}
