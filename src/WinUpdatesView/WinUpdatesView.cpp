// WinUpdatesView.cpp -- entry point, command-line handling and GUI bootstrap for
// the WinUpdatesView recreation (Windows Updates History Viewer, x64).
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <algorithm>
#include "resource.h"
#include "Config.h"
#include "Columns.h"
#include "UpdateRecord.h"
#include "UpdateEnumerator.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

    // The /cfg argument, if present, so both the GUI and the export path read and
    // write the same .cfg file the switch names.
    std::wstring cfgPathArg()
    {
        int argc = 0;
        std::wstring result;
        if (LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc))
        {
            for (int i = 1; i + 1 < argc; ++i)
                if (ieq(argv[i], L"/cfg")) result = argv[i + 1];
            ::LocalFree(argv);
        }
        return result;
    }

    // A /sort token: column index (or title) with an optional '~' = descending.
    struct SortSpec { int col; bool asc; };

    int columnFromName(const std::wstring& name)
    {
        wchar_t* end = nullptr;
        long idx = wcstol(name.c_str(), &end, 10);
        if (end && *end == 0) return (idx >= 0 && idx < kColumnCount) ? (int)idx : -1;
        for (int i = 0; i < kColumnCount; ++i)
            if (ieq(name, kColumns[i].title)) return i;
        return -1;
    }

    std::vector<UpdateRecord> filterItems(const std::vector<UpdateRecord>& all, const Config& cfg)
    {
        std::vector<UpdateRecord> out;
        for (const auto& r : all)
        {
            if (cfg.hideWindowsDefender &&
                (r.category == L"Microsoft Defender Antivirus" || r.category == L"Windows Defender"))
                continue;
            out.push_back(r);
        }
        return out;
    }

    void applySorts(std::vector<UpdateRecord>& items, const std::vector<SortSpec>& sorts, const DisplayOptions& d)
    {
        // Apply from last to first so the first /sort is the primary key.
        for (auto it = sorts.rbegin(); it != sorts.rend(); ++it)
        {
            int col = it->col; bool asc = it->asc;
            std::stable_sort(items.begin(), items.end(), [&](const UpdateRecord& a, const UpdateRecord& b) {
                bool na = false, nb = false;
                long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
                int c;
                if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                else          c = _wcsicmp(a.column(col, d).c_str(), b.column(col, d).c_str());
                return asc ? (c < 0) : (c > 0);
            });
        }
    }

    // Returns true if a CLI export was handled (caller should exit).
    bool runCommandLine(int& exitCode)
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (!argv) return false;

        Config cfg;
        if (std::wstring cp = cfgPathArg(); !cp.empty()) cfg.setCfgPath(cp);
        cfg.load();

        ExportFormat fmt{};
        std::wstring outFile;
        bool haveExport = false;
        std::vector<SortSpec> sorts;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : std::wstring(); };

            if (ieq(a, L"/cfg"))                   next();  // path already applied before load
            else if (ieq(a, L"/noloadsettings"))   { /* start with defaults */ cfg = Config(); }
            else if (ieq(a, L"/LoadFrom"))         cfg.loadFrom = _wtoi(next().c_str());
            else if (ieq(a, L"/ExternalFilename")) cfg.externalFilename = next();
            else if (ieq(a, L"/ComputerName"))     cfg.computerName = next();
            else if (ieq(a, L"/StopWinUpdatesService")) cfg.stopWinUpdatesService = _wtoi(next().c_str()) != 0;
            else if (ieq(a, L"/sort"))
            {
                std::wstring t = next();
                bool asc = true;
                if (!t.empty() && t[0] == L'~') { asc = false; t.erase(0, 1); }
                int col = columnFromName(t);
                if (col >= 0) sorts.push_back({ col, asc });
            }
            else
            {
                ExportFormat f{};
                if (FormatFromSwitch(a, f)) { fmt = f; outFile = next(); haveExport = true; }
            }
        }
        ::LocalFree(argv);

        if (!haveExport) return false;

        // Only the API path is implemented for headless export.
        HRESULT hr = ::CoInitialize(nullptr);
        HRESULT q = S_OK;
        std::vector<UpdateRecord> all = EnumerateUpdateHistory(&q);
        if (SUCCEEDED(hr)) ::CoUninitialize();

        std::vector<UpdateRecord> items = filterItems(all, cfg);

        DisplayOptions d; d.showTimeInGmt = cfg.showTimeInGmt;
        if (sorts.empty()) sorts.push_back({ cfg.sortColumn, cfg.sortAscending });
        applySorts(items, sorts, d);

        ExportOptions opt; opt.addHeaderLine = cfg.addExportHeaderLine; opt.display = d;
        bool ok = WriteReport(outFile, items, cfg.visibleColumns, fmt, opt);
        exitCode = ok ? 0 : 1;
        return true;
    }

    int runGui(int nCmdShow)
    {
        ::CoInitialize(nullptr);
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        if (std::wstring cp = cfgPathArg(); !cp.empty()) frame.m_cfg.setCfgPath(cp);
        // Original creates the main window with CW_USEDEFAULT X/Y/W/H (sub_1400122C4),
        // so the WTL default rect already matches -- do not pass a fixed rect.
        if (frame.CreateEx() == nullptr)
        {
            ::CoUninitialize();
            return 0;
        }
        frame.ShowWindow(frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        ::CoUninitialize();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    PinToLightMode(); // keep controls light, matching the original on a dark-mode system
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"WinUpdatesView", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);

    int exitCode = 0;
    if (runCommandLine(exitCode))
    {
        _Module.Term();
        return exitCode;
    }

    int ret = runGui(nCmdShow);
    _Module.Term();
    return ret;
}
