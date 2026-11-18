// TaskSchedulerView.cpp -- entry point, command-line handling and GUI bootstrap
// for the TaskSchedulerView recreation (NirSoft Task Scheduler viewer, x64).
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
#include "TaskRecord.h"
#include "TaskEnumerator.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

    // The /cfg argument, if present, so both the GUI and the headless path read and
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

    // Parse a /Columns "a,b,c" list into a display order.
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

    std::vector<TaskRecord> filterItems(const std::vector<TaskRecord>& all, const Config& cfg)
    {
        std::vector<TaskRecord> out;
        for (const auto& r : all)
        {
            if (cfg.hideDisabledTasks && r.cells[1] == L"Disabled") continue;
            if (cfg.hideHiddenTasks && r.cells[2] == L"Yes") continue;
            if (cfg.showOnlyRunningTasks && r.cells[1] != L"Running") continue;
            if (cfg.hideMicrosoftTasks && r.cells[7].rfind(L"\\Microsoft", 0) == 0) continue;
            out.push_back(r);
        }
        return out;
    }

    void applySorts(std::vector<TaskRecord>& items, const std::vector<SortSpec>& sorts)
    {
        for (auto it = sorts.rbegin(); it != sorts.rend(); ++it)
        {
            int col = it->col; bool asc = it->asc;
            std::stable_sort(items.begin(), items.end(), [&](const TaskRecord& a, const TaskRecord& b) {
                bool na = false, nb = false;
                long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
                int c;
                if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                else          c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
                return asc ? (c < 0) : (c > 0);
            });
        }
    }

    // Returns true if a headless command (export or a task action) was handled.
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
        bool noSort = false;
        std::vector<int> cliColumns;
        std::vector<std::pair<TaskOp, std::wstring>> actions;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : std::wstring(); };

            if (ieq(a, L"/cfg"))                   next();  // path already applied before load
            else if (ieq(a, L"/nosaveload"))       cfg = Config();
            else if (ieq(a, L"/LoadFrom"))         cfg.loadFrom = _wtoi(next().c_str());
            else if (ieq(a, L"/ComputerName"))     cfg.computerName = next();
            else if (ieq(a, L"/Columns"))          cliColumns = parseColumnList(next());
            else if (ieq(a, L"/nosort"))           noSort = true;
            else if (ieq(a, L"/savelangfile"))     { /* language-file dump, not reconstructed */ }
            else if (ieq(a, L"/RunAsAdmin"))       { /* elevation request, ignored headless */ }
            else if (ieq(a, L"/deleteregkey"))     { ::RegDeleteKeyW(HKEY_CURRENT_USER, kConfigKey); }
            else if (ieq(a, L"/Disable"))          actions.push_back({ TaskOp::Disable, next() });
            else if (ieq(a, L"/Enable"))           actions.push_back({ TaskOp::Enable, next() });
            else if (ieq(a, L"/StopTask"))         actions.push_back({ TaskOp::Stop, next() });
            else if (ieq(a, L"/RunNow"))           actions.push_back({ TaskOp::Run, next() });
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

        if (!haveExport && actions.empty()) return false;

        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        // Task actions (/Disable, /Enable, /StopTask, /RunNow) by task path or name.
        std::wstring comp = cfg.loadFrom == 2 ? cfg.computerName : std::wstring();
        for (auto& [op, name] : actions)
        {
            std::wstring path = name;
            if (!path.empty() && path[0] != L'\\') path = L"\\" + path;  // bare name -> root task
            TaskOperation(op, path, comp);
        }

        if (haveExport)
        {
            HRESULT q = S_OK;
            DisplayOptions d; d.showTimeInGmt = cfg.showTimeInGmt;
            std::vector<TaskRecord> all = EnumerateTasks(d, comp, &q);
            std::vector<TaskRecord> items = filterItems(all, cfg);

            if (!noSort)
            {
                if (sorts.empty()) sorts.push_back({ cfg.sortColumn, cfg.sortAscending });
                applySorts(items, sorts);
            }

            std::vector<int> columns = cliColumns.empty() ? cfg.visibleColumns : cliColumns;
            ExportOptions opt; opt.addHeaderLine = cfg.addExportHeaderLine; opt.encoding = cfg.saveFileEncoding;
            bool ok = WriteReport(outFile, items, columns, fmt, opt);
            exitCode = ok ? 0 : 1;
        }

        if (SUCCEEDED(hr)) ::CoUninitialize();
        return true;
    }

    int runGui(int nCmdShow)
    {
        ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        if (std::wstring cp = cfgPathArg(); !cp.empty()) frame.m_cfg.setCfgPath(cp);
        // Original CreateWindowExW passes CW_USEDEFAULT for X, Y, width and height, so let WTL use its default rect.
        if (frame.CreateEx(nullptr) == nullptr)
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
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"TaskSchedulerView", MB_ICONERROR);
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
