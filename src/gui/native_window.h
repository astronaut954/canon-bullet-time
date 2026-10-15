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

    // Mac: menu do app na barra de cima (Stillpoint, Edit, View, Window, Help).
    // Os itens do próprio app chamam onItem com "about", "updates", "settings", "help",
    // "cameras", "whatsnew" ou "report"; Quit passa pelo mesmo aviso do botão de fechar.
    // Windows: não faz nada (o menu fica num botão dentro da página).
    void InstallAppMenu(void* nativeWindow, std::function<void(const std::string&)> onItem);
}
