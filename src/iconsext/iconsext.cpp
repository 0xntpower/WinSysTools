// iconsext.cpp -- entry point. Recreation of IconsExtract v1.47 (x86).
// The GUI path mirrors the original WinMain (sub_40626D). The /save switch
// reproduces its command-line extract-and-quit mode (sub_4060D5), and -scanpath
// pre-fills the Search dialog (sub_4051F2 tail).
#include <atlbase.h>
#include <atlapp.h>
#include <vector>
#include <string>
#include <fstream>
#include "MainFrame.h"
#include "ResourceScan.h"
#include "IconSave.h"

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

    bool has(const std::vector<std::wstring>& a, const wchar_t* sw)
    {
        for (const auto& s : a) if (_wcsicmp(s.c_str(), sw) == 0) return true;
        return false;
    }

    // iconsext.exe /save "source file" "save folder" [-icons] [-cursors] [-asico]
    bool runSave(const std::vector<std::wstring>& a)
    {
        if (a.empty() || _wcsicmp(a[0].c_str(), L"/save") != 0) return false;
        if (a.size() < 3) return true;                 // switch present, nothing to do

        IconScanner scanner;
        scanner.options.icons = has(a, L"-icons");
        scanner.options.cursors = has(a, L"-cursors");
        scanner.options.subdirs = false;
        scanner.scanFile(a[1]);

        SaveEntries(a[2], scanner.results, has(a, L"-asico"));
        return true;
    }

    // iconsext.exe /savelangfile -- write the translation template next to the exe.
    // The recognised switch produces a seed file. The original dumps every UI
    // string. ponytail: minimal template, not the full original string set.
    bool runSaveLangFile(const std::vector<std::wstring>& a)
    {
        if (a.empty() || _wcsicmp(a[0].c_str(), L"/savelangfile") != 0) return false;
        wchar_t path[MAX_PATH];
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p = path;
        if (auto dot = p.find_last_of(L'.'); dot != std::wstring::npos) p.erase(dot);
        p += L"_lng.ini";
        std::ofstream f(p, std::ios::binary);
        f << "; IconsExtract language file\r\n[General]\r\n"
             "charset=\r\nTranslatorName=\r\nTranslatorURL=\r\nrtl=0\r\n";
        return true;
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    const auto a = args();

    if (runSaveLangFile(a)) return 0;
    if (runSave(a)) return 0;

    ::OleInitialize(nullptr);              // SHBrowseForFolder / shell APIs
    PinToLightMode();
    ::InitCommonControls();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), 0xFFFF };
    ::InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_startShowCmd = nCmdShow;
        frame.m_noLoadSettings = has(a, L"/noloadsettings");

        // -scanpath <path|wildcard> pre-fills the search path (sub_4051F2 tail).
        for (size_t i = 0; i < a.size(); ++i)
            if (_wcsicmp(a[i].c_str(), L"-scanpath") == 0 && i + 1 < a.size())
                frame.m_scanPathOverride = a[i + 1];

        RECT rc = { 0, 0, 640, 480 }; // original launches at (0,0), 640x480
        if (frame.CreateEx(nullptr, rc) == nullptr) return 1;
        frame.ShowWindow(frame.m_startShowCmd);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    ::OleUninitialize();
    return 0;
}
