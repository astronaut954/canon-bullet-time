#include "native_window.h"

// Usa o runtime do Objective-C pela API em C (sem precisar de arquivo .mm),
// do mesmo jeito que a biblioteca webview faz.
#include <objc/message.h>
#include <objc/runtime.h>

#include <string>

namespace
{
    std::function<bool()> g_canClose;

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
}
