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

    std::vector<UsbDevice> CanonUsbDevices(int& totalUsb)
    {
        // Ainda não feito no Windows: sem a lista do USB, o app usa só o SDK
        totalUsb = -1;
        return {};
    }

    bool RelaunchAfterExit()
    {
        std::vector<wchar_t> path(MAX_PATH);
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        while (length == path.size())
        {
            path.resize(path.size() * 2);
            length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        }
        if (length == 0)
            return false;

        // A nova cópia recebe o número deste processo e espera ele terminar
        SetEnvironmentVariableW(L"CBT_WAIT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const BOOL ok = CreateProcessW(path.data(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
        SetEnvironmentVariableW(L"CBT_WAIT_PID", nullptr);
        if (!ok)
            return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    void WaitForPreviousInstance()
    {
        wchar_t value[32] = {};
        const DWORD length = GetEnvironmentVariableW(L"CBT_WAIT_PID", value, 32);
        if (length == 0 || length >= 32)
            return;
        SetEnvironmentVariableW(L"CBT_WAIT_PID", nullptr);
        const DWORD previous = static_cast<DWORD>(wcstoul(value, nullptr, 10));
        // No máximo 20 s: a cópia anterior fecha em poucos segundos
        if (HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, previous))
        {
            WaitForSingleObject(handle, 20000);
            CloseHandle(handle);
        }
    }
}
