// ShadowCopyView.cpp -- entry point, command-line handling and GUI bootstrap for the
// ShadowCopyView recreation (NirSoft Volume Shadow Copy viewer, x64). The GUI mirrors
// the original window (snapshot list over file list). The headless modes reproduce the
// /stext|/stab|/stabular|/scomma|/shtml|/sverhtml|/sxml snapshot export, /CopyFile,
// /savelangfile, /nosaveload and /deleteregkey switches.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "Records.h"
#include "VssEnumerator.h"
#include "Export.h"
#include "MainFrame.h"

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
    int indexOf(const std::vector<std::wstring>& a, const wchar_t* sw)
    {
        for (int i = 0; i < (int)a.size(); ++i) if (_wcsicmp(a[i].c_str(), sw) == 0) return i;
        return -1;
    }
    std::wstring at(const std::vector<std::wstring>& a, int i)
    {
        return (i >= 0 && i < (int)a.size()) ? a[i] : std::wstring();
    }

    void sortSnaps(std::vector<SnapshotRecord>& rows, const ColumnState& st)
    {
        int col = st.sortColumn;
        bool asc = st.sortAscending;
        std::stable_sort(rows.begin(), rows.end(), [&](const SnapshotRecord& a, const SnapshotRecord& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
            int c;
            if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    // /s* <file>: enumerate snapshots and export the list, then quit.
    bool runExportCli(const std::vector<std::wstring>& a, bool noSaveLoad)
    {
        ExportFormat fmt{};
        int fmtPos = -1;
        for (size_t i = 0; i < a.size(); ++i)
            if (FormatFromSwitch(a[i], fmt)) { fmtPos = (int)i; break; }
        if (fmtPos < 0) return false;

        std::wstring file = at(a, fmtPos + 1);

        Config cfg;
        if (!noSaveLoad) cfg.load();

        ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::vector<SnapshotRecord> snaps = EnumerateSnapshots(DisplayOptions{}, nullptr);
        sortSnaps(snaps, cfg.snapCols);

        ExportRequest r;
        r.colDefs = kSnapColumns;
        r.columns = cfg.snapCols.visible;
        r.reportTitle = L"Shadow Copies List";
        r.rowCount = (int)snaps.size();
        r.cell = [&snaps](int row, int col) { return snaps[row].column(col); };
        WriteReport(file, r, fmt);
        ::CoUninitialize();
        return true;
    }

    // /CopyFile <ShadowCopy> <SourcePath> <DestFilename>
    bool runCopyFileCli(const std::vector<std::wstring>& a)
    {
        int i = indexOf(a, L"/CopyFile");
        if (i < 0) return false;
        std::wstring shadow = at(a, i + 1);
        std::wstring source = at(a, i + 2);
        std::wstring dest = at(a, i + 3);
        ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CopyFileFromShadow(shadow, source, dest);
        ::CoUninitialize();
        return true;
    }

    void saveLangFile()
    {
        std::wstring p = Config::DefaultPath();
        size_t dot = p.rfind(L".cfg");
        if (dot != std::wstring::npos) p.replace(dot, std::wstring::npos, L"_lng.ini");
        std::ofstream f(p, std::ios::binary);
        f << "[General]\r\nTranslatorName=\r\nTranslatorURL=\r\n";
    }

    int runGui(int nCmdShow, bool noSaveLoad)
    {
        ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = noSaveLoad;
        // The original CreateWindowExW passes CW_USEDEFAULT for X, Y, width and height,
        // so let WTL use its default rect. Saved placement overrides on later launches.
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
    const auto a = args();
    const bool noSaveLoad = indexOf(a, L"/nosaveload") >= 0;

    if (indexOf(a, L"/savelangfile") >= 0) { saveLangFile(); return 0; }
    if (indexOf(a, L"/deleteregkey") >= 0) { ::DeleteFileW(Config::DefaultPath().c_str()); return 0; }
    if (runCopyFileCli(a)) return 0;
    if (runExportCli(a, noSaveLoad)) return 0;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"ShadowCopyView", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    int ret = runGui(nCmdShow, noSaveLoad);
    _Module.Term();
    return ret;
}
