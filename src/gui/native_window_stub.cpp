// Somente para testes automatizados em Linux (não faz parte do app).
#include "native_window.h"
#include <cstdlib>
#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#endif

namespace NativeWindow
{
    std::string ChooseFolder(const std::string&)
    {
        const char* folder = std::getenv("CBT_TEST_SAVE_FOLDER");
        return folder ? folder : "";
    }

    void InstallCloseGuard(void* window, std::function<bool()> allowClose)
    {
#if __has_include(<gtk/gtk.h>)
        // Mesmo comportamento das versões Windows/Mac: o botão de fechar só
        // fecha a janela se allowClose() devolver true
        auto* guard = new std::function<bool()>(std::move(allowClose));
        g_signal_connect(G_OBJECT(window), "delete-event",
            G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean
            {
                return !(*static_cast<std::function<bool()>*>(data))();
            }),
            guard);
#else
        (void)window;
        (void)allowClose;
#endif
    }
}
