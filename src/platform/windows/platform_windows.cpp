#include "platform/platform.h"

#include <windows.h>
#include <objbase.h>
#include <setupapi.h>
#include <cwctype>
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

    bool SdkSessionsInWorkerThreads()
    {
        // Dado de 2026-10-14: aberta numa thread auxiliar, a sessão abria, mas os
        // comandos seguintes davam erro 2. Causa provável: o COM de cada thread é
        // separado, e a thread auxiliar encerrava o dela ao terminar.
        return false;
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

    namespace
    {
        std::string ToUtf8(const std::wstring& text)
        {
            if (text.empty())
                return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string out(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        // Propriedade de texto de um aparelho (várias linhas viram uma, separadas por \0)
        std::wstring DeviceProperty(HDEVINFO set, SP_DEVINFO_DATA& info, DWORD property)
        {
            DWORD size = 0;
            SetupDiGetDeviceRegistryPropertyW(set, &info, property, nullptr, nullptr, 0, &size);
            if (size == 0)
                return {};
            std::vector<BYTE> buffer(size + sizeof(wchar_t) * 2, 0);
            if (!SetupDiGetDeviceRegistryPropertyW(set, &info, property, nullptr, buffer.data(), size, nullptr))
                return {};
            const auto* text = reinterpret_cast<const wchar_t*>(buffer.data());
            return std::wstring(text, size / sizeof(wchar_t));
        }

        std::wstring Upper(std::wstring text)
        {
            for (auto& c : text)
                c = static_cast<wchar_t>(std::towupper(c));
            return text;
        }
    }

    std::vector<UsbDevice> CanonUsbDevices(int& totalUsb)
    {
        // Aparelhos USB presentes; fica com os da Canon (VID_04A9) que têm a interface
        // de imagem estática (classe 06, a das câmeras: impressoras e scanners ficam de fora)
        std::vector<UsbDevice> devices;
        totalUsb = -1;
        HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
        if (set == INVALID_HANDLE_VALUE)
            return devices;

        int total = 0;
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i)
        {
            ++total;
            const std::wstring hardware = Upper(DeviceProperty(set, info, SPDRP_HARDWAREID));
            const std::wstring compatible = Upper(DeviceProperty(set, info, SPDRP_COMPATIBLEIDS));
            if (hardware.find(L"VID_04A9") == std::wstring::npos || compatible.find(L"CLASS_06") == std::wstring::npos)
                continue;

            UsbDevice device;
            const size_t pid = hardware.find(L"PID_");
            if (pid != std::wstring::npos)
                device.productId = static_cast<int>(wcstol(hardware.substr(pid + 4, 4).c_str(), nullptr, 16));

            std::wstring name = DeviceProperty(set, info, SPDRP_FRIENDLYNAME);
            if (name.empty() || name[0] == L'\0')
                name = DeviceProperty(set, info, SPDRP_DEVICEDESC);
            device.name = ToUtf8(name.c_str());   // até o primeiro \0
            if (device.name.empty())
                device.name = "Canon USB device";

            wchar_t instance[512] = {};
            if (SetupDiGetDeviceInstanceIdW(set, &info, instance, 512, nullptr))
                device.location = ToUtf8(instance);
            devices.push_back(device);
        }
        SetupDiDestroyDeviceInfoList(set);
        totalUsb = total;
        return devices;
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
