// DebugView.cpp -- entry point and GUI bootstrap for the DebugView recreation.
//
// The tool captures Win32 user-mode debug output (OutputDebugStringA) through
// the DBWIN shared-buffer protocol and shows it in a report list. The kernel
// capture path, which needs the DbgView helper driver, is out of scope and is
// documented in analysis/DebugView.md. Command-line handling covers the
// Sysinternals -accepteula switch and /?.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include "resource.h"
#include "Config.h"
#include "Capture.h"
#include "Export.h"
#include "Eula.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const wchar_t* b)
    {
        return _wcsicmp(a.c_str(), b) == 0;
    }

    struct CmdLine
    {
        bool acceptEula = false;
        bool showHelp = false;
    };

    CmdLine parseCommandLine()
    {
        CmdLine c;
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (!argv)
        {
            return c;
        }
        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            if (ieq(a, L"-accepteula") || ieq(a, L"/accepteula"))
            {
                c.acceptEula = true;
            }
            else if (ieq(a, L"/?") || ieq(a, L"-?"))
            {
                c.showHelp = true;
            }
        }
        ::LocalFree(argv);
        return c;
    }

    int runGui(int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        // The original CreateWindowExA uses X=100, Y=100, width=500, height=300
        // on a first launch (sub_140008730), so pass that exact rect. A saved
        // placement from the registry overrides it inside OnCreate.
        RECT rc = {100, 100, 100 + 500, 100 + 300};
        if (frame.CreateEx(nullptr, rc) == nullptr)
        {
            return 0;
        }
        int show = frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow;
        frame.ShowWindow(show);
        frame.UpdateWindow();

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES |
                                              ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS};
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Error", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);

    CmdLine cmd = parseCommandLine();
    if (cmd.showHelp)
    {
        ::MessageBoxW(nullptr,
                      L"DebugView captures Win32 debug output.\n\n"
                      L"Usage: DebugView [-accepteula]\n\n"
                      L"-accepteula   accept the Sysinternals license without prompting.",
                      L"DebugView", MB_OK | MB_ICONINFORMATION);
        _Module.Term();
        return 0;
    }

    if (!Eula::EnsureAccepted(nullptr, cmd.acceptEula))
    {
        _Module.Term();
        return 0;
    }

    int ret = runGui(nCmdShow);
    _Module.Term();
    return ret;
}
