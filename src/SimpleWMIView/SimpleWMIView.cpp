// SimpleWMIView.cpp -- entry point, command-line handling and GUI bootstrap for
// the SimpleWMIView recreation (NirSoft WMI/WQL query viewer, x64).
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
#include "WmiResult.h"
#include "WmiQuery.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

    // The /cfg argument, if present, so the store reads and writes the file the
    // switch names before the first load.
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

    struct SortSpec { int col; bool asc; };

    std::vector<std::wstring> splitComma(const std::wstring& s)
    {
        std::vector<std::wstring> out;
        std::wstring cur;
        for (wchar_t c : s)
        {
            if (c == L',') { out.push_back(cur); cur.clear(); }
            else cur += c;
        }
        out.push_back(cur);
        for (auto& t : out)
        {
            size_t a = t.find_first_not_of(L" \t");
            size_t b = t.find_last_not_of(L" \t");
            t = (a == std::wstring::npos) ? std::wstring() : t.substr(a, b - a + 1);
        }
        return out;
    }

    int columnFromToken(const WmiResult& res, const std::wstring& token)
    {
        wchar_t* end = nullptr;
        long idx = wcstol(token.c_str(), &end, 10);
        if (end && *end == 0) return (idx >= 0 && idx < (int)res.columns.size()) ? (int)idx : -1;
        return res.columnIndex(token);
    }

    void applySorts(WmiResult& res, std::vector<int>& order, const std::vector<SortSpec>& sorts, const DisplayOptions& d)
    {
        for (auto it = sorts.rbegin(); it != sorts.rend(); ++it)
        {
            int col = it->col; bool asc = it->asc;
            if (col < 0 || col >= (int)res.columns.size()) continue;
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const Cell& ca = res.rows[a].cells[col];
                const Cell& cb = res.rows[b].cells[col];
                long long ka, kb;
                int c;
                if (ca.numericKey(ka) && cb.numericKey(kb))
                    c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                else
                    c = _wcsicmp(ca.text(d).c_str(), cb.text(d).c_str());
                return asc ? (c < 0) : (c > 0);
            });
        }
    }

    // Returns true if a headless export was handled (caller should exit).
    bool runCommandLine(Config& cfg, bool& wantUpdate, int& exitCode)
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (!argv) return false;

        ExportFormat fmt{};
        std::wstring outFile;
        bool haveExport = false;
        std::vector<SortSpec> sortTokensRaw;      // resolved after the query
        std::vector<std::wstring> sortRaw;
        std::vector<std::wstring> columnNames;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : std::wstring(); };

            if (ieq(a, L"/cfg"))                  next();  // path already applied before load
            else if (ieq(a, L"/Namespace"))       cfg.nameSpace = next();
            else if (ieq(a, L"/Class"))           cfg.classOrQuery = next();
            else if (ieq(a, L"/ConnectComputer")) cfg.connectComputer = _wtoi(next().c_str());
            else if (ieq(a, L"/ComputerName"))    cfg.computerName = next();
            else if (ieq(a, L"/SyncMode"))        cfg.syncMode = _wtoi(next().c_str());
            else if (ieq(a, L"/Update"))          wantUpdate = true;
            else if (ieq(a, L"/SaveDirect"))      { /* streamed save, same output here */ }
            else if (ieq(a, L"/RunAsAdmin"))      { /* elevation not reconstructed */ }
            else if (ieq(a, L"/sort"))            sortRaw.push_back(next());
            else if (ieq(a, L"/Columns"))         columnNames = splitComma(next());
            else
            {
                ExportFormat f{};
                if (FormatFromSwitch(a, f)) { fmt = f; outFile = next(); haveExport = true; }
            }
        }
        ::LocalFree(argv);

        if (!haveExport) return false;

        WmiQueryParams p;
        p.nameSpace = cfg.nameSpace;
        p.classOrQuery = cfg.classOrQuery;
        p.connectComputer = cfg.connectComputer;
        p.computerName = cfg.computerName;
        p.userName = cfg.userName;

        WmiResult res = RunWmiQuery(p);

        DisplayOptions d;
        d.showTimeInGmt = cfg.showTimeInGmt;
        d.showMilliseconds = cfg.showMilliseconds;

        std::vector<int> order;
        for (int i = 0; i < (int)res.rows.size(); ++i) order.push_back(i);

        std::vector<SortSpec> sorts;
        for (auto& t : sortRaw)
        {
            std::wstring token = t;
            bool asc = true;
            if (!token.empty() && token[0] == L'~') { asc = false; token.erase(0, 1); }
            int col = columnFromToken(res, token);
            if (col >= 0) sorts.push_back({ col, asc });
        }
        if (sorts.empty() && cfg.sortColumn < (int)res.columns.size())
            sorts.push_back({ cfg.sortColumn, cfg.sortAscending });
        applySorts(res, order, sorts, d);

        // Reorder the rows into the sorted order.
        WmiResult sorted;
        sorted.columns = res.columns;
        for (int i : order) sorted.rows.push_back(res.rows[i]);

        ExportOptions opt;
        opt.display = d;
        opt.encoding = cfg.saveFileEncoding;
        if (!columnNames.empty())
        {
            for (auto& n : columnNames)
            {
                int c = sorted.columnIndex(n);
                if (c >= 0) opt.columns.push_back(c);
            }
        }

        bool ok = WriteReport(outFile, sorted, fmt, opt);
        exitCode = ok ? 0 : 1;
        return true;
    }

    int runGui(Config& cfg, bool wantUpdate, int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_cfg = cfg;             // seed from CLI overrides
        frame.m_autoUpdate = wantUpdate;
        // Original CreateWindowExW passes CW_USEDEFAULT for X, Y, width and height, so let WTL use its default rect.
        if (frame.CreateEx(nullptr) == nullptr)
            return 0;
        // Re-seed the bar from the CLI-overridden config (OnCreate loaded from
        // the .cfg file, so push the final config back into the bar).
        frame.m_bar.FromConfig(frame.m_cfg);
        frame.ShowWindow(frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow);
        frame.UpdateWindow();
        if (wantUpdate) frame.PostMessage(WM_COMMAND, MAKEWPARAM(ID_SWV_REFRESH, 0));

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    ::InitCommonControlsEx(&icc);

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ::CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    _Module.Init(nullptr, hInstance);

    Config cfg;
    if (std::wstring cp = cfgPathArg(); !cp.empty()) cfg.setCfgPath(cp);
    cfg.load();

    int exitCode = 0;
    bool wantUpdate = false;
    if (runCommandLine(cfg, wantUpdate, exitCode))
    {
        _Module.Term();
        ::CoUninitialize();
        return exitCode;
    }

    int ret = runGui(cfg, wantUpdate, nCmdShow);
    _Module.Term();
    ::CoUninitialize();
    return ret;
}
