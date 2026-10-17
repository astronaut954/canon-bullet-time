#include "native_window.h"

// Usa o runtime do Objective-C pela API em C (sem precisar de arquivo .mm),
// do mesmo jeito que a biblioteca webview faz.
#include <objc/message.h>
#include <objc/runtime.h>
#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    std::function<bool()> g_canClose;                     // janela principal (também o Quit do menu)
    std::map<id, std::function<bool()>> g_closeGuards;    // por janela (principal e Live)
    std::function<void(const std::string&)> g_onMenuItem;
    std::vector<std::string> g_menuItems;  // posição (tag do item) -> nome
    id g_window = nullptr;

    // Teclas modificadoras dos atalhos (NSEventModifierFlag...)
    const unsigned long kCommand = 1ul << 20, kOption = 1ul << 19, kControl = 1ul << 18;

    template <typename Result, typename... Args>
    Result Send(id object, const char* selector, Args... args)
    {
        using Fn = Result (*)(id, SEL, Args...);
        return reinterpret_cast<Fn>(objc_msgSend)(object, sel_registerName(selector), args...);
    }

    id GetClass(const char* name)
    {
        return reinterpret_cast<id>(objc_getClass(name));
    }

    id NSStringFrom(const std::string& text)
    {
        return Send<id>(GetClass("NSString"), "stringWithUTF8String:", text.c_str());
    }

    BOOL WindowShouldClose(id, SEL, id sender)
    {
        const auto it = g_closeGuards.find(sender);
        if (it == g_closeGuards.end() || !it->second)
            return YES;
        return it->second() ? YES : NO;
    }

    bool IsNativeFullScreen(id window)
    {
        return (Send<unsigned long>(window, "styleMask") & (1ul << 14)) != 0;  // NSWindowStyleMaskFullScreen
    }

    id ScreenNamed(const std::string& name)
    {
        id screens = Send<id>(GetClass("NSScreen"), "screens");
        const unsigned long count = Send<unsigned long>(screens, "count");
        for (unsigned long i = 0; i < count; ++i)
        {
            id screen = Send<id>(screens, "objectAtIndex:", i);
            id localized = Send<id>(screen, "localizedName");
            const char* utf8 = localized ? Send<const char*>(localized, "UTF8String") : nullptr;
            if (utf8 && name == utf8)
                return screen;
        }
        return nullptr;
    }

    // ---- Menu do app ----

    void MenuItemChosen(id, SEL, id sender)
    {
        const long tag = Send<long>(sender, "tag");
        if (tag < 0 || tag >= static_cast<long>(g_menuItems.size()))
            return;
        const std::string& name = g_menuItems[static_cast<size_t>(tag)];
        // Quit: mesma pergunta do botão de fechar (sem o "performClose:", que apita
        // quando a página segura o fechamento para mostrar o aviso)
        if (name == "quit")
        {
            if (g_window && (!g_canClose || g_canClose()))
                Send<void>(g_window, "close");
            return;
        }
        if (g_onMenuItem)
            g_onMenuItem(name);
    }

    // Objeto que recebe os cliques dos itens do próprio app (About, Settings...)
    id MenuTarget()
    {
        static id target = nullptr;
        if (target)
            return target;
        ::Class cls = objc_getClass("StillpointMenuTarget");
        if (!cls)
        {
            cls = objc_allocateClassPair(objc_getClass("NSObject"), "StillpointMenuTarget", 0);
            class_addMethod(cls, sel_registerName("menuItem:"), reinterpret_cast<IMP>(MenuItemChosen), "v@:@");
            objc_registerClassPair(cls);
        }
        target = Send<id>(Send<id>(reinterpret_cast<id>(cls), "alloc"), "init");
        return target;
    }

    id NewMenu(const std::string& title)
    {
        return Send<id>(Send<id>(GetClass("NSMenu"), "alloc"), "initWithTitle:", NSStringFrom(title));
    }

    // Item com uma ação padrão do Mac (copy:, hide:...). Sem target: vai para quem
    // estiver em foco (a página, a janela ou o app). modifiers 0 = só Command.
    id AddItem(id menu, const std::string& title, const char* action, const std::string& key = "",
               unsigned long modifiers = 0, id target = nullptr)
    {
        id item = Send<id>(Send<id>(GetClass("NSMenuItem"), "alloc"), "initWithTitle:action:keyEquivalent:",
                           NSStringFrom(title), action ? sel_registerName(action) : static_cast<SEL>(nullptr),
                           NSStringFrom(key));
        if (modifiers)
            Send<void>(item, "setKeyEquivalentModifierMask:", modifiers);
        if (target)
            Send<void>(item, "setTarget:", target);
        Send<void>(menu, "addItem:", item);
        return item;
    }

    // Item do próprio app: avisa a página com o nome
    void AddAppItem(id menu, const std::string& title, const std::string& name, const std::string& key = "")
    {
        id item = AddItem(menu, title, "menuItem:", key, 0, MenuTarget());
        Send<void>(item, "setTag:", static_cast<long>(g_menuItems.size()));
        g_menuItems.push_back(name);
    }

    void AddSeparator(id menu)
    {
        Send<void>(menu, "addItem:", Send<id>(GetClass("NSMenuItem"), "separatorItem"));
    }

    void AddToBar(id bar, id menu)
    {
        id item = AddItem(bar, "", nullptr);
        Send<void>(item, "setSubmenu:", menu);
    }
}

namespace NativeWindow
{
    std::string ChooseFolder(const std::string& prompt)
    {
        id panel = Send<id>(GetClass("NSOpenPanel"), "openPanel");
        Send<void>(panel, "setCanChooseFiles:", NO);
        Send<void>(panel, "setCanChooseDirectories:", YES);
        Send<void>(panel, "setCanCreateDirectories:", YES);
        Send<void>(panel, "setAllowsMultipleSelection:", NO);
        Send<void>(panel, "setMessage:", NSStringFrom(prompt));
        Send<void>(panel, "setPrompt:", NSStringFrom("Choose"));

        const long response = Send<long>(panel, "runModal");
        if (response != 1) // NSModalResponseOK
            return "";

        id url = Send<id>(panel, "URL");
        id path = Send<id>(url, "path");
        const char* utf8 = Send<const char*>(path, "UTF8String");
        return utf8 ? utf8 : "";
    }

    void InstallCloseGuard(void* nativeWindow, std::function<bool()> canClose)
    {
        id window = static_cast<id>(nativeWindow);
        if (g_closeGuards.empty())
            g_canClose = canClose;  // a primeira é a janela principal (Quit do menu)
        g_closeGuards[window] = std::move(canClose);

        // A janela já tem um "delegate" criado pela biblioteca webview (a mesma classe
        // para todas as janelas); acrescentamos a ele a pergunta "pode fechar?"
        // (windowShouldClose:), que procura a janela no mapa acima.
        id delegate = Send<id>(window, "delegate");
        if (!delegate)
            return;
        ::Class cls = object_getClass(delegate);
        class_addMethod(cls, sel_registerName("windowShouldClose:"),
                        reinterpret_cast<IMP>(WindowShouldClose), "B@:@");
    }

    // ---- Janelas Live ----

    std::vector<Screen> Screens()
    {
        std::vector<Screen> out;
        id screens = Send<id>(GetClass("NSScreen"), "screens");
        const unsigned long count = Send<unsigned long>(screens, "count");
        for (unsigned long i = 0; i < count; ++i)
        {
            id screen = Send<id>(screens, "objectAtIndex:", i);
            id localized = Send<id>(screen, "localizedName");
            const char* utf8 = localized ? Send<const char*>(localized, "UTF8String") : nullptr;
            Screen item;
            item.name = utf8 && *utf8 ? utf8 : "Display " + std::to_string(i + 1);
            item.primary = i == 0;
            out.push_back(item);
        }
        return out;
    }

    void Show(void* nativeWindow)
    {
        id window = static_cast<id>(nativeWindow);
        Send<void>(window, "makeKeyAndOrderFront:", nullptr);
        id app = Send<id>(GetClass("NSApplication"), "sharedApplication");
        Send<void>(app, "activateIgnoringOtherApps:", YES);
    }

    void Hide(void* nativeWindow)
    {
        Send<void>(static_cast<id>(nativeWindow), "orderOut:", nullptr);
    }

    bool SetFullScreen(void* nativeWindow, const std::string& screenName)
    {
        id window = static_cast<id>(nativeWindow);
        if (IsNativeFullScreen(window))
            return true;
        id screen = ScreenNamed(screenName);
        if (!screen)
            screen = Send<id>(window, "screen");
        if (!screen)
            screen = Send<id>(GetClass("NSScreen"), "mainScreen");
        if (!screen)
            return false;

        // Tela cheia do próprio sistema (como o botão verde), no monitor escolhido: a
        // janela vai primeiro para esse monitor e então entra em tela cheia lá. Com
        // "Displays have separate Spaces" (padrão do Mac), só esse monitor muda; o outro
        // continua normal. Tentativas anteriores (janela sem borda cobrindo o monitor)
        // perderam o teclado e as respostas da página (2026-10-16).
        const unsigned long titled = 1ul << 0, closable = 1ul << 1, miniaturizable = 1ul << 2, resizable = 1ul << 3;
        Send<void>(window, "setStyleMask:", titled | closable | miniaturizable | resizable);
        Send<void>(window, "setLevel:", 0l);
        Send<void>(window, "setCollectionBehavior:", (1ul << 7));  // NSWindowCollectionBehaviorFullScreenPrimary
        const CGRect visible = Send<CGRect>(screen, "visibleFrame");
        const CGRect current = Send<CGRect>(window, "frame");
        const CGFloat w = std::min(current.size.width, visible.size.width), h = std::min(current.size.height, visible.size.height);
        const CGRect frame = CGRectMake(visible.origin.x + (visible.size.width - w) / 2,
                                        visible.origin.y + (visible.size.height - h) / 2, w, h);
        Send<void>(window, "setFrame:display:", frame, YES);
        Send<void>(window, "makeKeyAndOrderFront:", nullptr);
        Send<void>(window, "toggleFullScreen:", nullptr);
        return true;
    }

    void SetWindowed(void* nativeWindow, bool alwaysOnTop)
    {
        id window = static_cast<id>(nativeWindow);
        if (IsNativeFullScreen(window))
        {
            // Sai da tela cheia do sistema (animação; a janela volta ao tamanho de antes)
            Send<void>(window, "toggleFullScreen:", nullptr);
            Send<void>(window, "setLevel:", alwaysOnTop ? 3l : 0l);
            return;
        }
        Send<void>(window, "setLevel:", alwaysOnTop ? 3l : 0l);  // NSFloatingWindowLevel : NSNormalWindowLevel
        Send<void>(window, "makeKeyAndOrderFront:", nullptr);
    }
}
