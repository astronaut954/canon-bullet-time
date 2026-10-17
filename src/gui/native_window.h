#pragma once

// Recursos da janela que dependem do sistema (Windows / Mac).

#include <functional>
#include <string>
#include <vector>

namespace NativeWindow
{
    // Abre o seletor de pastas do sistema. Retorna "" se a pessoa cancelar.
    // Chamar na thread da janela.
    std::string ChooseFolder(const std::string& prompt);

    // Antes de a janela fechar, pergunta a canClose(): se devolver false, a janela
    // continua aberta (a página mostra a confirmação e encerra depois). Vale por
    // janela: pode ser instalado na principal e nas janelas Live.
    void InstallCloseGuard(void* nativeWindow, std::function<bool()> canClose);

    // Mac: menu do app na barra de cima (Stillpoint, Edit, View, Window, Help).
    // Os itens do próprio app chamam onItem com "about", "updates", "settings", "help",
    // "cameras", "whatsnew" ou "report"; Quit passa pelo mesmo aviso do botão de fechar.
    // Windows: não faz nada (o menu fica num botão dentro da página).
    void InstallAppMenu(void* nativeWindow, std::function<void(const std::string&)> onItem);

    // ---- Janelas Live (segunda janela: tela cheia num monitor ou flutuante) ----

    struct Screen
    {
        std::string name;   // nome para mostrar ("Built-in Display", "LG TV", "Display 2")
        bool primary = false;
    };
    // Monitores ligados agora, na ordem do sistema (o principal costuma ser o primeiro).
    std::vector<Screen> Screens();

    // Mostra de novo uma janela escondida (e traz para a frente) / esconde sem fechar.
    void Show(void* nativeWindow);
    void Hide(void* nativeWindow);

    // Tela cheia no monitor com esse nome (sem borda, cobrindo o monitor inteiro, por cima
    // da barra de menus e da barra de tarefas). Nome desconhecido: usa o monitor em que a
    // janela está. Retorna false se não deu.
    bool SetFullScreen(void* nativeWindow, const std::string& screenName);

    // Volta a ser uma janela normal (borda, botões, tamanho livre), sempre à frente ou não.
    void SetWindowed(void* nativeWindow, bool alwaysOnTop);
}
