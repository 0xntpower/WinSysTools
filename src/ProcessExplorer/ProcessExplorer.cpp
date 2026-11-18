// ProcessExplorer.cpp -- entry point, EULA gate and GUI bootstrap for the
// Process Explorer recreation, a source-accurate live process viewer (x64).
//
// The command line carries -accepteula (both -accepteula and /accepteula
// spellings), which records acceptance without prompting. Acceptance is stored
// under HKCU\Software\Sysinternals\Process Explorer, value EulaAccepted
// (REG_DWORD 1), exactly as the original does. The main window uses the
// system default position and size on first run (the original passes
// CW_USEDEFAULT) and restores its saved WindowPlacement on later runs.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
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

    int RunGui(int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        // The original creates its main window with CW_USEDEFAULT, so pass the
        // WTL default rect and let the system place and size it on first run.
        if (frame.CreateEx() == nullptr)
        {
            return 0;
        }
        frame.ShowWindow(frame.m_havePlacement ? frame.m_placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        const int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const std::vector<std::wstring> a = Args();
    const bool acceptFlag = HasFlag(a, L"-accepteula") || HasFlag(a, L"/accepteula");

    if (!Eula::EnsureAccepted(nullptr, acceptFlag))
    {
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES |
                                              ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LINK_CLASS};
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Process Explorer", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    const int ret = RunGui(nCmdShow);
    _Module.Term();
    return ret;
}
