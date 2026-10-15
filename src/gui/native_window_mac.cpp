#include "native_window.h"

// Usa o runtime do Objective-C pela API em C (sem precisar de arquivo .mm),
// do mesmo jeito que a biblioteca webview faz.
#include <objc/message.h>
#include <objc/runtime.h>

#include <string>
#include <vector>

namespace
{
    std::function<bool()> g_canClose;
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

    BOOL WindowShouldClose(id, SEL, id)
    {
        return (!g_canClose || g_canClose()) ? YES : NO;
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
        g_canClose = std::move(canClose);

        // A janela já tem um "delegate" criado pela biblioteca webview; acrescentamos
        // a ele a pergunta "pode fechar?" (windowShouldClose:).
        id window = static_cast<id>(nativeWindow);
        id delegate = Send<id>(window, "delegate");
        if (!delegate)
            return;
        ::Class cls = object_getClass(delegate);
        class_addMethod(cls, sel_registerName("windowShouldClose:"),
                        reinterpret_cast<IMP>(WindowShouldClose), "B@:@");
    }

    void InstallAppMenu(void* nativeWindow, std::function<void(const std::string&)> onItem)
    {
        g_onMenuItem = std::move(onItem);
        g_menuItems.clear();
        g_window = static_cast<id>(nativeWindow);
        id app = Send<id>(GetClass("NSApplication"), "sharedApplication");
        // Uma janela só: sem abas (o Mac acrescentaria "Show Tab Bar" no menu View)
        Send<void>(GetClass("NSWindow"), "setAllowsAutomaticWindowTabbing:", NO);
        id bar = NewMenu("");

        // Stillpoint
        id appMenu = NewMenu("Stillpoint");
        AddAppItem(appMenu, "About Stillpoint", "about");
        AddAppItem(appMenu, "Check for Updates…", "updates");
        AddSeparator(appMenu);
        AddAppItem(appMenu, "Settings…", "settings", ",");
        AddSeparator(appMenu);
        id services = NewMenu("Services");
        id servicesItem = AddItem(appMenu, "Services", nullptr);
        Send<void>(servicesItem, "setSubmenu:", services);
        Send<void>(app, "setServicesMenu:", services);
        AddSeparator(appMenu);
        AddItem(appMenu, "Hide Stillpoint", "hide:", "h");
        AddItem(appMenu, "Hide Others", "hideOtherApplications:", "h", kCommand | kOption);
        AddItem(appMenu, "Show All", "unhideAllApplications:");
        AddSeparator(appMenu);
        // Quit: como clicar no botão de fechar (com os mesmos avisos de fotos não salvas)
        AddAppItem(appMenu, "Quit Stillpoint", "quit", "q");
        AddToBar(bar, appMenu);

        // Edit: sem ele o Mac não aceita ⌘C, ⌘V e ⌘A nos campos de texto
        id edit = NewMenu("Edit");
        AddItem(edit, "Undo", "undo:", "z");
        AddItem(edit, "Redo", "redo:", "Z");
        AddSeparator(edit);
        AddItem(edit, "Cut", "cut:", "x");
        AddItem(edit, "Copy", "copy:", "c");
        AddItem(edit, "Paste", "paste:", "v");
        AddItem(edit, "Select All", "selectAll:", "a");
        AddToBar(bar, edit);

        // View
        id view = NewMenu("View");
        AddItem(view, "Enter Full Screen", "toggleFullScreen:", "f", kCommand | kControl);
        AddToBar(bar, view);

        // Window
        id windowMenu = NewMenu("Window");
        AddItem(windowMenu, "Minimize", "performMiniaturize:", "m");
        AddItem(windowMenu, "Zoom", "performZoom:");
        AddSeparator(windowMenu);
        AddItem(windowMenu, "Bring All to Front", "arrangeInFront:");
        AddToBar(bar, windowMenu);
        Send<void>(app, "setWindowsMenu:", windowMenu);

        // Help
        id help = NewMenu("Help");
        AddAppItem(help, "Stillpoint Help", "help");
        AddAppItem(help, "Compatible Cameras", "cameras");
        AddAppItem(help, "What's New", "whatsnew");
        AddSeparator(help);
        AddAppItem(help, "Report a Problem", "report");
        AddToBar(bar, help);
        Send<void>(app, "setHelpMenu:", help);

        Send<void>(app, "setMainMenu:", bar);
    }
}
