#include "platform/platform.h"

#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shellapi.h>
#include <setupapi.h>
#include <cwctype>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <fstream>
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

    fs::path ResourcesDirectory()
    {
        return ExecutableDirectory();
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

    namespace
    {
        // O app se chamava "Canon Bullet Time": se só a pasta antiga existe, ela é
        // renomeada para a nova (fotos guardadas e modelos aprendidos vão junto).
        fs::path MigrateDataFolder(const fs::path& base)
        {
            const fs::path current = base / "Stillpoint";
            const fs::path old = base / "Canon Bullet Time";
            std::error_code ec;
            if (!fs::exists(current, ec) && fs::is_directory(old, ec))
            {
                fs::rename(old, current, ec);
                if (ec)
                    return old;
            }
            return current;
        }
    }

    fs::path AppDataFolder()
    {
        char* localAppData = nullptr;
        size_t len = 0;

        if (_dupenv_s(&localAppData, &len, "LOCALAPPDATA") == 0 && localAppData != nullptr)
        {
            const fs::path base(localAppData);
            free(localAppData);
            return MigrateDataFolder(base);
        }

        return MigrateDataFolder(DownloadsFolder());
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
        SetEnvironmentVariableW(L"STILLPOINT_WAIT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const BOOL ok = CreateProcessW(path.data(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
        SetEnvironmentVariableW(L"STILLPOINT_WAIT_PID", nullptr);
        if (!ok)
            return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    void WaitForPreviousInstance()
    {
        wchar_t value[32] = {};
        const DWORD length = GetEnvironmentVariableW(L"STILLPOINT_WAIT_PID", value, 32);
        if (length == 0 || length >= 32)
            return;
        SetEnvironmentVariableW(L"STILLPOINT_WAIT_PID", nullptr);
        const DWORD previous = static_cast<DWORD>(wcstoul(value, nullptr, 10));
        // No máximo 20 s: a cópia anterior fecha em poucos segundos
        if (HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, previous))
        {
            WaitForSingleObject(handle, 20000);
            CloseHandle(handle);
        }
    }

    // ---------------------------------------------------------------------
    // Atualização do app
    // ---------------------------------------------------------------------
    namespace
    {
        std::wstring Widen(const std::string& text)
        {
            if (text.empty())
                return std::wstring();
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring out(size, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
            return out;
        }

        // Regras de aspas da linha de comando do Windows (CommandLineToArgvW)
        std::wstring QuoteArg(const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
                return arg;
            std::wstring out = L"\"";
            size_t slashes = 0;
            for (wchar_t c : arg)
            {
                if (c == L'\\') { ++slashes; continue; }
                if (c == L'"') out.append(slashes * 2 + 1, L'\\');
                else out.append(slashes, L'\\');
                slashes = 0;
                out += c;
            }
            out.append(slashes * 2, L'\\');
            return out + L"\"";
        }
    }

    fs::path CurlExecutable()
    {
        wchar_t windir[MAX_PATH] = {};
        const UINT length = GetSystemDirectoryW(windir, MAX_PATH);
        return length > 0 ? fs::path(std::wstring(windir, length)) / L"curl.exe" : fs::path(L"curl.exe");
    }

    int RunProcess(const std::vector<std::string>& args, int timeoutSeconds, const std::atomic<bool>* cancel)
    {
        if (args.empty())
            return -1;
        std::wstring commandLine;
        for (const auto& arg : args)
            commandLine += (commandLine.empty() ? L"" : L" ") + QuoteArg(Widen(arg));
        const std::wstring program = Widen(args[0]);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &process))
            return -1;
        CloseHandle(process.hThread);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        int result = -1;
        for (;;)
        {
            if (WaitForSingleObject(process.hProcess, 200) == WAIT_OBJECT_0)
            {
                DWORD code = 0;
                GetExitCodeProcess(process.hProcess, &code);
                result = static_cast<int>(code);
                break;
            }
            if ((cancel && cancel->load()) || std::chrono::steady_clock::now() > deadline)
            {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 2000);
                break;
            }
        }
        CloseHandle(process.hProcess);
        return result;
    }

    std::string Sha256OfFile(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return "";
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
            return "";
        std::string hex;
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0)
        {
            std::vector<char> buffer(1 << 16);
            bool ok = true;
            while (ok && in)
            {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto got = in.gcount();
                if (got > 0)
                    ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(got), 0) == 0;
            }
            unsigned char digest[32] = {};
            if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
            {
                static const char* digits = "0123456789abcdef";
                for (unsigned char b : digest)
                {
                    hex += digits[b >> 4];
                    hex += digits[b & 15];
                }
            }
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return hex;
    }

    bool CanSelfUpdate(std::string& reason)
    {
        // Instalado pelo instalador (Inno Setup): o desinstalador fica ao lado do .exe
        std::error_code ec;
        if (fs::exists(ExecutableDirectory() / "unins000.exe", ec))
            return true;
        reason = "This copy was not installed with the Stillpoint installer.";
        return false;
    }

    bool StartUpdateInstall(const fs::path& installer, std::string& error)
    {
        // Um PowerShell escondido roda o instalador em modo silencioso (ele pede permissão
        // de administrador para Arquivos de Programas), espera terminar e abre o Stillpoint
        // de novo. Assim a reabertura não depende do instalador baixado. Se a permissão for
        // negada, o app abre de novo na versão atual.
        wchar_t system[MAX_PATH] = {};
        const UINT length = GetSystemDirectoryW(system, MAX_PATH);
        if (length == 0)
        {
            error = "The installer could not start.";
            return false;
        }
        const std::wstring powershell = std::wstring(system, length) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";

        std::vector<wchar_t> exe(MAX_PATH);
        DWORD exeLength = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        while (exeLength == exe.size())
        {
            exe.resize(exe.size() * 2);
            exeLength = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        }
        // Texto entre aspas simples do PowerShell: ' vira ''
        auto quoted = [](std::wstring text)
        {
            for (size_t pos = 0; (pos = text.find(L'\'', pos)) != std::wstring::npos; pos += 2)
                text.insert(pos, 1, L'\'');
            return L"'" + text + L"'";
        };
        const std::wstring script =
            L"Remove-Item Env:STILLPOINT_TEST_VERSION -ErrorAction SilentlyContinue; "
            L"try { Start-Process -FilePath " + quoted(installer.wstring()) +
            L" -ArgumentList '/SILENT /SP- /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS' -Wait -ErrorAction Stop } catch { } "
            L"Start-Process -FilePath " + quoted(std::wstring(exe.data(), exeLength));

        std::wstring commandLine = QuoteArg(powershell) +
            L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command " + QuoteArg(script);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(powershell.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            error = "The installer could not start.";
            return false;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    void OpenUrl(const std::string& url)
    {
        ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
