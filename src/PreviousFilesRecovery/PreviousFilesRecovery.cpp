// PreviousFilesRecovery.cpp -- entry point. The GUI path mirrors the original WinMain
// (the window hosts the search panel over the shadow-copy scan). The command-line
// "save and quit" mode reproduces /stext|/stab|/stabular|/scomma|/shtml|/sverhtml|
// /sxml with /sort, /cfg, /nosaveload and /savelangfile.
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
#include "Config.h"
#include "Columns.h"
#include "PrevFileRecord.h"
#include "Scanner.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const wchar_t* b)
    {
        return _wcsicmp(a.c_str(), b) == 0;
    }

    std::vector<std::wstring> args()
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

    int columnFromToken(const std::wstring& tok)
    {
        if (!tok.empty() && (iswdigit(tok[0]) || tok[0] == L'-'))
        {
            int n = _wtoi(tok.c_str());
            if (n >= 0 && n < kColumnCount)
            {
                return n;
            }
        }
        for (int c = 0; c < kColumnCount; ++c)
        {
            if (_wcsicmp(tok.c_str(), kColumns[c].title) == 0)
            {
                return c;
            }
        }
        return -1;
    }

    void applySort(const std::vector<std::wstring>& a, std::vector<PrevFileRecord>& rows, const DisplayOptions& d)
    {
        std::vector<std::pair<int, bool>> keys; // column, ascending
        for (size_t i = 0; i + 1 < a.size(); ++i)
        {
            if (ieq(a[i], L"/sort"))
            {
                std::wstring tok = a[i + 1];
                bool asc = true;
                if (!tok.empty() && tok[0] == L'~')
                {
                    asc = false;
                    tok.erase(0, 1);
                }
                int col = columnFromToken(tok);
                if (col >= 0)
                {
                    keys.emplace_back(col, asc);
                }
            }
        }
        if (keys.empty())
        {
            return;
        }
        for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        {
            int col = it->first;
            bool asc = it->second;
            std::stable_sort(rows.begin(), rows.end(),
                             [&](const PrevFileRecord& x, const PrevFileRecord& y)
                             {
                                 bool nx = false, ny = false;
                                 long long kx = x.sortKey(col, nx), ky = y.sortKey(col, ny);
                                 int c;
                                 if (nx && ny)
                                 {
                                     c = (kx < ky) ? -1 : (kx > ky) ? 1 : 0;
                                 }
                                 else
                                 {
                                     c = _wcsicmp(x.column(col, d).c_str(), y.column(col, d).c_str());
                                 }
                                 return asc ? (c < 0) : (c > 0);
                             });
        }
    }

    int indexOf(const std::vector<std::wstring>& a, const wchar_t* sw)
    {
        for (int i = 0; i < (int)a.size(); ++i)
        {
            if (ieq(a[i], sw))
            {
                return i;
            }
        }
        return -1;
    }
    std::wstring at(const std::vector<std::wstring>& a, int i)
    {
        return (i >= 0 && i < (int)a.size()) ? a[i] : std::wstring();
    }

    void saveLangFile()
    {
        std::wstring p = DefaultConfigPath();
        size_t dot = p.rfind(L".cfg");
        if (dot != std::wstring::npos)
        {
            p.replace(dot, std::wstring::npos, L"_lng.ini");
        }
        std::ofstream f(p, std::ios::binary);
        f << "[general]\r\nrtl=0\r\ncharset=\r\nTranslatorName=\r\nTranslatorURL=\r\n";
    }

    // /s* <file>: scan with the configured options, export and quit.
    bool runExportCli(const std::vector<std::wstring>& a)
    {
        ExportFormat fmt{};
        int fmtPos = -1;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (FormatFromSwitch(a[i], fmt))
            {
                fmtPos = (int)i;
                break;
            }
        }
        if (fmtPos < 0)
        {
            return false;
        }

        std::wstring file = at(a, fmtPos + 1); // "" means stdout

        Config cfg;
        int cfgPos = indexOf(a, L"/cfg");
        if (cfgPos >= 0)
        {
            wchar_t expanded[MAX_PATH]{};
            ::ExpandEnvironmentStringsW(at(a, cfgPos + 1).c_str(), expanded, MAX_PATH);
            if (expanded[0])
            {
                cfg.path = expanded;
            }
        }
        if (indexOf(a, L"/nosaveload") < 0)
        {
            cfg.load();
        }

        ScanOptions opt;
        opt.baseFolder = cfg.baseFolder;
        opt.wildcard = cfg.filesWildcard;
        opt.depth = cfg.subfoldersDepth;
        opt.findPrevVersions = cfg.findPreviousFiles;
        opt.findDeletedFiles = cfg.findDeletedFiles;
        opt.findDeletedFolders = cfg.findDeletedFolders;
        opt.mode = cfg.multipleCopiesMode;
        opt.disp.showTimeInGmt = cfg.showTimeInGmt;

        std::vector<PrevFileRecord> rows = ScanPreviousFiles(opt);
        applySort(a, rows, opt.disp);

        ExportRequest r;
        r.columns = cfg.visibleColumns;
        r.rowCount = (int)rows.size();
        DisplayOptions d = opt.disp;
        r.cell = [&rows, d](int row, int col)
        {
            return rows[row].column(col, d);
        };
        WriteReport(file, r, fmt);
        return true;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    PinToLightMode(); // before any themed control initializes, per the shared helper
    const auto a = args();

    ::CoInitialize(nullptr);

    if (indexOf(a, L"/savelangfile") >= 0)
    {
        saveLangFile();
        ::CoUninitialize();
        return 0;
    }
    if (runExportCli(a))
    {
        ::CoUninitialize();
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc),
                             ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Error", MB_ICONERROR);
        ::CoUninitialize();
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = indexOf(a, L"/nosaveload") >= 0;

        // Original CreateWindowExW: default position (CW_USEDEFAULT) with a fixed 640x480 size.
        RECT rc = {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT + 640, CW_USEDEFAULT + 480};
        if (frame.CreateEx(nullptr, rc) == nullptr)
        {
            _Module.Term();
            ::CoUninitialize();
            return 1;
        }
        frame.ShowWindow(frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    ::CoUninitialize();
    return 0;
}
