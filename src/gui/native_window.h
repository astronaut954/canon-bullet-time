#pragma once

// Recursos da janela que dependem do sistema (Windows / Mac).

#include <functional>
#include <string>

namespace NativeWindow
{
    // Abre o seletor de pastas do sistema. Retorna "" se a pessoa cancelar.
    // Chamar na thread da janela.
    std::string ChooseFolder(const std::string& prompt);

    // Antes de a janela fechar, pergunta a canClose(): se devolver false, a janela
    // continua aberta (a página mostra a confirmação e encerra depois).
    void InstallCloseGuard(void* nativeWindow, std::function<bool()> canClose);
}
