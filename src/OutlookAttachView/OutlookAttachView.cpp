// OutlookAttachView.cpp -- entry point, command-line handling and GUI bootstrap
// for the OutlookAttachView recreation (NirSoft Outlook attachment viewer, x64).
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
#include "AttachRecord.h"
#include "MapiScanner.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

    struct SortSpec { int col; bool asc; };

    std::wstring stripSpaces(std::wstring s)
    {
        s.erase(std::remove(s.begin(), s.end(), L' '), s.end());
        return s;
    }

    int columnFromName(const std::wstring& name)
    {
        wchar_t* end = nullptr;
        long idx = wcstol(name.c_str(), &end, 10);
        if (end && *end == 0) return (idx >= 0 && idx < kColumnCount) ? (int)idx : -1;
        for (int i = 0; i < kColumnCount; ++i)
            if (ieq(name, kColumns[i].title) || ieq(stripSpaces(name), stripSpaces(kColumns[i].title)))
                return i;
        return -1;
    }

    std::vector<int> parseColumnList(const std::wstring& list)
    {
        std::vector<int> cols;
        size_t start = 0;
        while (start <= list.size())
        {
            size_t comma = list.find(L',', start);
            std::wstring name = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            while (!name.empty() && name.front() == L' ') name.erase(name.begin());
            while (!name.empty() && name.back() == L' ') name.pop_back();
            int c = columnFromName(name);
            if (c >= 0) cols.push_back(c);
            if (comma == std::wstring::npos) break;
            start = comma + 1;
        }
        return cols;
    }

    void applySorts(std::vector<AttachRecord>& items, const std::vector<SortSpec>& sorts)
    {
        for (auto it = sorts.rbegin(); it != sorts.rend(); ++it)
        {
            int col = it->col; bool asc = it->asc;
            std::stable_sort(items.begin(), items.end(), [&](const AttachRecord& a, const AttachRecord& b) {
                bool na = false, nb = false;
                long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
                int c;
                if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                else          c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
                return asc ? (c < 0) : (c > 0);
            });
        }
    }

    // Returns true if a headless export command was handled.
    bool runCommandLine(int& exitCode)
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (!argv) return false;

        Config cfg;
        cfg.load();

        ExportFormat fmt{};
        std::wstring outFile;
        bool haveExport = false;
        std::vector<SortSpec> sorts;
        bool noSort = false;
        std::vector<int> cliColumns;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : std::wstring(); };

            if (ieq(a, L"/cfg"))             { cfg.path = next(); cfg.load(); }
            else if (ieq(a, L"/nosaveload")) { cfg = Config(); }
            else if (ieq(a, L"/Columns"))    cliColumns = parseColumnList(next());
            else if (ieq(a, L"/nosort"))     noSort = true;
            else if (ieq(a, L"/SaveDirect")) { /* streamed-save mode, loads into memory here */ }
            else if (ieq(a, L"/start"))      { /* GUI immediate scan, handled in the window */ }
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

        // Scan the mailbox and write the report. With no profile the scan returns
        // no rows and the report is written empty, matching a headless run with no
        // Outlook session.
        ScanParams params;
        params.display.showTimeInGmt = cfg.showTimeInGmt;
        params.display.fileSizeUnit = cfg.fileSizeUnit;
        params.showInlineAttachments = cfg.showInlineAttachment;
        ScanResult r = ScanMailbox(params);
        std::vector<AttachRecord> items = std::move(r.records);

        if (!noSort)
        {
            if (sorts.empty()) sorts.push_back({ cfg.sortColumn, cfg.sortAscending });
            applySorts(items, sorts);
        }

        std::vector<int> columns = cliColumns.empty() ? cfg.visibleColumns : cliColumns;
        ExportOptions opt;
        opt.addHeaderLine = cfg.addExportHeaderLine;
        opt.encoding = cfg.saveFileEncoding;
        exitCode = WriteReport(outFile, items, columns, fmt, opt) ? 0 : 1;
        return true;
    }

    int runGui(int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        // The original CreateWindowExW passes CW_USEDEFAULT for X, Y, width and
        // height, so let WTL use its default rect. Saved placement overrides.
        if (frame.CreateEx(nullptr) == nullptr)
            return 0;
        frame.ShowWindow(frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"OutlookAttachView", MB_ICONERROR);
        return 1;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    _Module.Init(nullptr, hInstance);

    int exitCode = 0;
    if (runCommandLine(exitCode))
    {
        _Module.Term();
        ::CoUninitialize();
        return exitCode;
    }

    int ret = runGui(nCmdShow);
    _Module.Term();
    ::CoUninitialize();
    return ret;
}
