// ProductKeyScanner.cpp -- entry point. Recreation of NirSoft ProductKeyScanner
// (x64, Unicode, v1.03). The GUI path mirrors the original WinMain. The recovered
// switches /nosaveload, /cfg, /savelangfile, /deleteregkey are reproduced, and the
// /s* export switches plus a /selftest decoder check are added (the original v1.03
// has no command-line export -- see analysis/ProductKeyScanner.md).
#include <atlbase.h>
#include <atlapp.h>
#include <vector>
#include <string>
#include <cstdio>
#include "MainFrame.h"
#include "Scanner.h"
#include "Export.h"
#include "Decoder.h"

// Request comctl32 v6 for the modern visual styles the original gets from its
// embedded manifest. The linker folds this into the generated application manifest.
#pragma comment(linker, "\"/manifestdependency:type='win32' "                      \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "                   \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

    // /selftest -- decode two known DigitalProductId vectors (generated offline by an
    // independent base-24 encoder) and assert the product keys. Classic path and the
    // Windows 8+ 'N'-insertion path. Returns 0 on success.
    int selfTest()
    {
        if (::AttachConsole(ATTACH_PARENT_PROCESS))
        {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
        }
        struct Vec { uint8_t region[15]; const wchar_t* key; };
        const Vec vecs[] = {
            { { 0x3b,0x35,0x72,0xd4,0xc9,0x3e,0,0,0,0,0,0,0,0,0 }, L"BBBBB-BBBBB-BBBBC-DFGHJ-KMPQR" },
            { { 0x9f,0x56,0x7d,0x2d,0x52,0xcf,0x16,0,0,0,0,0,0,0,0x08 }, L"NBBBB-BBBBB-BBBGH-JKMPQ-RTVWX" },
        };
        for (const auto& v : vecs)
        {
            std::wstring got = DecodeProductKeyRegion(v.region);
            if (got != v.key)
            {
                wprintf(L"SELFTEST FAIL: region decode got [%s] expected [%s]\n", got.c_str(), v.key);
                return 1;
            }
            // Also exercise the DigitalProductId blob path (region at offset 52).
            std::vector<uint8_t> blob(0xA4, 0);
            memcpy(blob.data() + 52, v.region, 15);
            std::wstring got2 = DecodeProductKeyBlob(blob.data(), blob.size());
            if (got2 != v.key)
            {
                wprintf(L"SELFTEST FAIL: blob decode got [%s] expected [%s]\n", got2.c_str(), v.key);
                return 1;
            }
        }
        wprintf(L"SELFTEST OK\n");
        return 0;
    }

    // "save and quit" export: /stext|/stab|/sjson|/shtml|/sverhtml|/sxml|/scomma <file>.
    // Scans the running system with the saved/default options. Added in the recreation.
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
                ScanOptions opts;  // current system, WMI + BIOS on
                WriteReport(file, EnumerateProductKeys(opts), cols, fmt);
                return true;
            }
        }
        return false;
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    const auto a = args();

    if (has(a, L"/selftest"))
        return selfTest();

    if (has(a, L"/deleteregkey"))
    {
        ::RegDeleteKeyW(HKEY_CURRENT_USER, kConfigKey);
        return 0;
    }

    // /savelangfile and /cfg are recognized switches (see analysis/ProductKeyScanner.md).
    if (runCli(a))
        return 0;

    PinToLightMode(); // keep controls light, matching the original on a dark-mode system

    ::InitCommonControls();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES };
    ::InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = has(a, L"/nosaveload");
        for (size_t i = 0; i + 1 < a.size(); ++i)
            if (_wcsicmp(a[i].c_str(), L"/cfg") == 0) frame.SetCfgPath(a[i + 1]);
        frame.m_startShowCmd = nCmdShow;
        if (frame.CreateEx() == nullptr)
            return 1;
        frame.ShowWindow(frame.m_startShowCmd);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    return 0;
}
