#include "native_window.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace
{
    // Uma pergunta "pode fechar?" por janela (guardada no subclass da janela)
    LRESULT CALLBACK CloseGuardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
    {
        auto* canClose = reinterpret_cast<std::function<bool()>*>(data);
        if (msg == WM_CLOSE && canClose && *canClose && !(*canClose)())
            return 0; // a página vai perguntar e encerrar depois (ou a janela Live só se esconde)
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    struct MonitorItem { HMONITOR handle; RECT rect; bool primary; std::wstring device; };
    std::vector<MonitorItem> Monitors()
    {
        std::vector<MonitorItem> list;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM param) -> BOOL
        {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(handle, &info))
                reinterpret_cast<std::vector<MonitorItem>*>(param)->push_back(
                    { handle, info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0, info.szDevice });
            return TRUE;
        }, reinterpret_cast<LPARAM>(&list));
        // O principal primeiro, como no Mac
        for (size_t i = 1; i < list.size(); ++i)
            if (list[i].primary)
                std::swap(list[0], list[i]);
        return list;
    }
    std::string MonitorName(const std::vector<MonitorItem>& list, size_t i)
    {
        return "Display " + std::to_string(i + 1) + (list[i].primary ? " (main)" : "");
    }

    std::wstring ToWide(const std::string& text)
    {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wide(size > 0 ? size - 1 : 0, L'\0');
        if (size > 0)
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
        return wide;
    }

    std::string ToUtf8(const wchar_t* text)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size > 0 ? size - 1 : 0, '\0');
        if (size > 0)
            WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), size, nullptr, nullptr);
        return utf8;
    }
}

namespace NativeWindow
{
    std::string ChooseFolder(const std::string& prompt)
    {
        std::string result;
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return result;

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        const std::wstring title = ToWide(prompt);
        dialog->SetTitle(title.c_str());

        if (SUCCEEDED(dialog->Show(GetActiveWindow())))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                {
                    result = ToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
        return result;
    }

    void InstallCloseGuard(void* nativeWindow, std::function<bool()> canClose)
    {
        auto* guard = new std::function<bool()>(std::move(canClose));  // vive enquanto a janela
        SetWindowSubclass(static_cast<HWND>(nativeWindow), CloseGuardProc, 1, reinterpret_cast<DWORD_PTR>(guard));
    }

    // ---- Janelas Live ----

    std::vector<Screen> Screens()
    {
        std::vector<Screen> out;
        const auto list = Monitors();
        for (size_t i = 0; i < list.size(); ++i)
            out.push_back({ MonitorName(list, i), list[i].primary });
        return out;
    }

    void Show(void* nativeWindow)
    {
        HWND hwnd = static_cast<HWND>(nativeWindow);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    }

    void Hide(void* nativeWindow)
    {
        ShowWindow(static_cast<HWND>(nativeWindow), SW_HIDE);
    }

    bool SetFullScreen(void* nativeWindow, const std::string& screenName)
    {
        HWND hwnd = static_cast<HWND>(nativeWindow);
        const auto list = Monitors();
        if (list.empty())
            return false;
        RECT rect = list[0].rect;
        bool found = false;
        for (size_t i = 0; i < list.size(); ++i)
            if (MonitorName(list, i) == screenName)
            {
                rect = list[i].rect;
                found = true;
            }
        if (!found)
        {
            // O monitor em que a janela está agora
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
                rect = info.rcMonitor;
        }
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        return true;
    }

    void SetWindowed(void* nativeWindow, bool alwaysOnTop)
    {
        HWND hwnd = static_cast<HWND>(nativeWindow);
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const bool wasFull = (style & WS_POPUP) != 0;
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        if (wasFull)
        {
            // Tamanho de janela, no meio do monitor principal
            const auto list = Monitors();
            RECT area = list.empty() ? RECT{ 0, 0, 1280, 800 } : list[0].rect;
            const int w = 960, h = 560;
            const int x = area.left + ((area.right - area.left) - w) / 2;
            const int y = area.top + ((area.bottom - area.top) - h) / 2;
            SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else
            SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
    }

    // No Windows o menu fica num botão dentro da página (assets/ui)
    void InstallAppMenu(void*, std::function<void(const std::string&)>) {}
}
