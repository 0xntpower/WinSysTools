// Autoruns.cpp -- entry point, EULA gate and GUI bootstrap for the Autoruns
// recreation, a source-accurate autostart-entry viewer (x64).
//
// The command line carries -accepteula (both -accepteula and /accepteula
// spellings), which records acceptance without prompting. Acceptance is stored
// under HKCU\Software\Sysinternals\Autoruns, value EulaAccepted (REG_DWORD 1),
// exactly as the original does (sub_140062810). Autoruns runs asInvoker and
// reads only what the current token can see, so no privileges are enabled here.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include "resource.h"
#include "MainFrame.h"
#include "Eula.h"

CAppModule _Module;

namespace
{
    std::vector<std::wstring> Args()
    {
        std::vector<std::wstring> v;
        int n = 0;
        if (LPWSTR* a = ::CommandLineToArgvW(::GetCommandLineW(), &n))
        {
            for (int i = 1; i < n; ++i)
            {
                v.emplace_back(a[i]);
            }
            ::LocalFree(a);
        }
        return v;
    }

    bool HasFlag(const std::vector<std::wstring>& a, const wchar_t* flag)
    {
        for (const std::wstring& s : a)
        {
            if (_wcsicmp(s.c_str(), flag) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // The original is an MFC frame that restores its window placement from the
    // registry and opens at a wide default on first launch. With no saved
    // placement, open a wide window centered on the work area.
    void FirstLaunchRect(RECT& rc)
    {
        const int w = 1000;
        const int h = 620;
        RECT work{};
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int x = work.left + ((work.right - work.left) - w) / 2;
        const int y = work.top + ((work.bottom - work.top) - h) / 2;
        ::SetRect(&rc, x, y, x + w, y + h);
    }

    int RunGui(int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        RECT rc{};
        FirstLaunchRect(rc);
        if (frame.CreateEx(nullptr, &rc) == nullptr)
        {
            return 0;
        }
        frame.ShowWindow(frame.m_havePlacement ? frame.m_placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        const int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const std::vector<std::wstring> a = Args();
    const bool acceptFlag = HasFlag(a, L"-accepteula") || HasFlag(a, L"/accepteula");

    if (!Eula::EnsureAccepted(nullptr, acceptFlag))
    {
        return 0;
    }

    // The Scheduled Tasks and Startup-folder providers use COM (the Task
    // Scheduler service and IShellLink).
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
        ICC_TAB_CLASSES | ICC_LINK_CLASS };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Autoruns", MB_ICONERROR);
        ::CoUninitialize();
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    const int ret = RunGui(nCmdShow);
    _Module.Term();
    ::CoUninitialize();
    return ret;
}
