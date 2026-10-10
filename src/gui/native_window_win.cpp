#include "native_window.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <string>

namespace
{
    std::function<bool()> g_canClose;

    LRESULT CALLBACK CloseGuardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (msg == WM_CLOSE && g_canClose && !g_canClose())
            return 0; // a página vai perguntar e encerrar depois
        return DefSubclassProc(hwnd, msg, wp, lp);
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
        g_canClose = std::move(canClose);
        SetWindowSubclass(static_cast<HWND>(nativeWindow), CloseGuardProc, 1, 0);
    }
}
