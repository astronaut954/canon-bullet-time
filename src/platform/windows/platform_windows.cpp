#include "platform/platform.h"

#include <windows.h>
#include <objbase.h>
#include <clocale>
#include <cstdlib>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Platform
{
    void InitConsole()
    {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        setlocale(LC_ALL, ".UTF8");
    }

    fs::path ExecutableDirectory()
    {
        std::vector<char> buffer(MAX_PATH);
        DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        while (length == buffer.size())
        {
            buffer.resize(buffer.size() * 2);
            length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        }

        if (length == 0)
        {
            return fs::current_path();
        }

        return fs::path(std::string(buffer.data(), length)).parent_path();
    }

    fs::path DownloadsFolder()
    {
        char* userProfile = nullptr;
        size_t len = 0;

        if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile != nullptr)
        {
            fs::path result = fs::path(userProfile) / "Downloads";
            free(userProfile);
            return result;
        }

        return fs::current_path();
    }

    std::tm LocalTime(std::time_t t)
    {
        std::tm result{};
        localtime_s(&result, &t);
        return result;
    }

    void ProcessEvents(int /*milliseconds*/)
    {
        // No Windows, os eventos do SDK chegam via EdsGetEvent(); nada a fazer aqui.
    }

    void BeginSdkThread()
    {
        // Exigência do EDSDK para chamadas fora da thread principal (manual da Canon).
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }

    void EndSdkThread()
    {
        CoUninitialize();
    }

    void DiscardPendingInput()
    {
        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    }

    fs::path AppDataFolder()
    {
        char* localAppData = nullptr;
        size_t len = 0;

        if (_dupenv_s(&localAppData, &len, "LOCALAPPDATA") == 0 && localAppData != nullptr)
        {
            fs::path result = fs::path(localAppData) / "Canon Bullet Time";
            free(localAppData);
            return result;
        }

        return DownloadsFolder() / "Canon Bullet Time";
    }

    std::vector<std::string> CanonUsbDevices()
    {
        // Ainda não feito no Windows (diagnóstico usado só no Mac por enquanto)
        return {};
    }
}
