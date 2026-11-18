// rdpv.cpp -- entry point. Recreation of RemoteDesktopPassView (x86, v1.02).
// GUI path mirrors the original WinMain (sub_4070E8). The /s* switches reproduce its
// command-line "save and quit" mode. /nosaveload, /noreg, /deleteregkey, /savelangfile
// and a /selftest crypto check are recovered too.
#include <atlbase.h>
#include <atlapp.h>
#include <vector>
#include <string>
#include "MainFrame.h"
#include "RdpPassword.h"
#include "Export.h"
#include "Columns.h"
#include "Dpapi.h"

CAppModule _Module;

namespace
{
    std::vector<std::wstring> args()
    {
        std::vector<std::wstring> v;
        int n = 0;
        if (LPWSTR* a = ::CommandLineToArgvW(::GetCommandLineW(), &n))
        {
            for (int i = 1; i < n; ++i) v.emplace_back(a[i]);
            ::LocalFree(a);
        }
        return v;
    }

    // "save and quit" -- /stext|/stab|/stabular|/shtml|/sverhtml|/sxml <file>.
    // Exports the default scan (My Documents *.rdp), matching the original.
    bool runCli(const std::vector<std::wstring>& a)
    {
        for (size_t i = 0; i < a.size(); ++i)
        {
            ExportFormat fmt;
            if (FormatFromSwitch(a[i], fmt))
            {
                std::wstring file = (i + 1 < a.size()) ? a[i + 1] : L"";
                if (file.empty()) return true;
                std::vector<int> cols;
                for (int c = 0; c < kColumnCount; ++c) cols.push_back(c);
                WriteReport(file, EnumerateRdpPasswords(), cols, fmt);
                return true;
            }
        }
        return false;
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    const auto a = args();

    bool noSaveLoad = false;
    for (const auto& s : a)
    {
        if (_wcsicmp(s.c_str(), L"/selftest") == 0)
            return dpapi::selfTest() ? 0 : 1;            // crypto self-check, no UI
        if (_wcsicmp(s.c_str(), L"/deleteregkey") == 0)
        {
            ::RegDeleteKeyW(HKEY_CURRENT_USER, kConfigKey);
            return 0;
        }
        if (_wcsicmp(s.c_str(), L"/nosaveload") == 0)
            noSaveLoad = true;
        // /savelangfile and /noreg are recognized switches (see analysis/rdpv.md).
    }

    if (runCli(a))
        return 0;

    PinToLightMode();
    ::InitCommonControls();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES };
    ::InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = noSaveLoad;
        frame.m_startShowCmd = nCmdShow;
        RECT rc = { 0, 0, 640, 480 }; // original launches at (0,0), 640x480
        if (frame.CreateEx(nullptr, rc) == nullptr)
            return 1;
        frame.ShowWindow(frame.m_startShowCmd);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    return 0;
}
