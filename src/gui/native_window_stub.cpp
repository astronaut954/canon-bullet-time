// Somente para testes automatizados em Linux (não faz parte do app).
#include "native_window.h"
#include <cstdlib>
namespace NativeWindow
{
    std::string ChooseFolder(const std::string&)
    {
        const char* folder = std::getenv("CBT_TEST_SAVE_FOLDER");
        return folder ? folder : "";
    }
    void InstallCloseGuard(void*, std::function<bool()>) {}
}
