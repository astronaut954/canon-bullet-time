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
    bool PlayWavMixed(const std::string& filePath);
}
