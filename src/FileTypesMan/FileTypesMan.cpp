// FileTypesMan.cpp -- entry point. A source-accurate recreation of FileTypesMan (x64, v2.01).
// The GUI path mirrors the original WinMain (two list panes over the HKCR scan). The
// command-line "save and quit" mode reproduces /stext|/stab|/scomma|/stabular|/shtml|
// /sverhtml|/sxml with /sort, /nosort, /SelectedExt, the option switches, /savelangfile
// and /nosaveload.
#include <atlbase.h>
#include <atlapp.h>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include "MainFrame.h"
#include "FileType.h"
#include "FileAction.h"
#include "Export.h"
#include "Columns.h"

CAppModule _Module;

namespace
{
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
    int indexOf(const std::vector<std::wstring>& a, const wchar_t* sw)
    {
        for (int i = 0; i < (int)a.size(); ++i)
        {
            if (_wcsicmp(a[i].c_str(), sw) == 0)
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

    std::vector<int> allColumns(int n)
    {
        std::vector<int> c;
        for (int i = 0; i < n; ++i)
        {
            c.push_back(i);
        }
        return c;
    }

    EnumOptions optsFromArgs(const std::vector<std::wstring>& a)
    {
        EnumOptions e;
        e.showUrlProtocols = false;
        e.showApplications = true;
        auto flag = [&](const wchar_t* sw, bool& out)
        {
            int i = indexOf(a, sw);
            if (i >= 0)
            {
                out = _wtoi(at(a, i + 1).c_str()) != 0;
            }
        };
        flag(L"/ShowURLProtocols", e.showUrlProtocols);
        flag(L"/ShowApplicationsTypes", e.showApplications);
        return e;
    }

    // Column index from a /sort token: a number, or a title (case-insensitive).
    int columnFromToken(const std::wstring& tok, const ColumnDef* defs, int count)
    {
        if (!tok.empty() && (iswdigit(tok[0]) || tok[0] == L'-'))
        {
            int n = _wtoi(tok.c_str());
            if (n >= 0 && n < count)
            {
                return n;
            }
        }
        for (int c = 0; c < count; ++c)
        {
            if (_wcsicmp(tok.c_str(), defs[c].title) == 0)
            {
                return c;
            }
        }
        return -1;
    }

    // Apply the /sort switches (multiple, '~' prefix = descending) and /nosort.
    template <class T>
    void applySort(const std::vector<std::wstring>& a, std::vector<T>& rows, const ColumnDef* defs, int count)
    {
        if (indexOf(a, L"/nosort") >= 0)
        {
            return;
        }
        std::vector<std::pair<int, bool>> keys; // column, ascending
        for (size_t i = 0; i + 1 < a.size(); ++i)
        {
            if (_wcsicmp(a[i].c_str(), L"/sort") == 0)
            {
                std::wstring tok = a[i + 1];
                bool asc = true;
                if (!tok.empty() && tok[0] == L'~')
                {
                    asc = false;
                    tok.erase(0, 1);
                }
                int col = columnFromToken(tok, defs, count);
                if (col >= 0)
                {
                    keys.emplace_back(col, asc);
                }
            }
        }
        if (keys.empty())
        {
            keys.emplace_back(0, true);
        }
        // Stable-sort from the last key to the first so earlier keys dominate.
        for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        {
            int col = it->first;
            bool asc = it->second;
            std::stable_sort(rows.begin(), rows.end(),
                             [&](const T& x, const T& y)
                             {
                                 int c = _wcsicmp(x.column(col).c_str(), y.column(col).c_str());
                                 return asc ? (c < 0) : (c > 0);
                             });
        }
    }

    // /s* <file>: export and quit. With /SelectedExt, export that extension's actions.
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
        int selExt = indexOf(a, L"/SelectedExt");
        bool header = false; // AddExportHeaderLine default 0

        if (selExt >= 0)
        {
            std::wstring ext = at(a, selExt + 1);
            auto types = EnumerateFileTypes(optsFromArgs(a));
            const FileType* match = nullptr;
            for (const auto& t : types)
            {
                if (_wcsicmp(t.key.c_str(), ext.c_str()) == 0)
                {
                    match = &t;
                    break;
                }
            }
            std::vector<FileAction> acts;
            if (match)
            {
                acts = EnumerateActions(*match, true, true);
            }
            applySort(a, acts, kActionColumns, kActionColumnCount);
            ExportRequest r;
            r.colDefs = kActionColumns;
            r.columns = allColumns(kActionColumnCount);
            r.reportTitle = L"File Type Actions";
            r.headerLine = header;
            r.rowCount = (int)acts.size();
            r.cell = [&acts](int row, int col)
            {
                return acts[row].column(col);
            };
            WriteReport(file, r, fmt);
            return true;
        }

        auto types = EnumerateFileTypes(optsFromArgs(a));
        applySort(a, types, kTypeColumns, kTypeColumnCount);
        ExportRequest r;
        r.colDefs = kTypeColumns;
        r.columns = allColumns(kTypeColumnCount);
        r.reportTitle = L"File Types List";
        r.headerLine = header;
        r.rowCount = (int)types.size();
        r.cell = [&types](int row, int col)
        {
            return types[row].column(col);
        };
        WriteReport(file, r, fmt);
        return true;
    }

    void saveLangFile()
    {
        std::wstring p = CMainFrame::ConfigPath();
        size_t dot = p.rfind(L".cfg");
        if (dot != std::wstring::npos)
        {
            p.replace(dot, std::wstring::npos, L"_lng.ini");
        }
        std::ofstream f(p, std::ios::binary);
        f << "[General]\r\nTranslatorName=\r\nTranslatorURL=\r\n";
    }
} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    const auto a = args();

    if (indexOf(a, L"/savelangfile") >= 0)
    {
        saveLangFile();
        return 0;
    }
    if (runExportCli(a))
    {
        return 0;
    }

    PinToLightMode();
    ::InitCommonControls();
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES};
    ::InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = indexOf(a, L"/nosaveload") >= 0;
        int selExt = indexOf(a, L"/SelectedExt");
        if (selExt >= 0)
        {
            frame.m_startSelectExt = at(a, selExt + 1);
        }

        // Original CreateWindowExW uses default position (CW_USEDEFAULT) with a fixed 640x480 size.
        RECT rc = {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT + 640, CW_USEDEFAULT + 480};
        if (frame.CreateEx(nullptr, rc) == nullptr)
        {
            return 1;
        }
        frame.ShowWindow(nCmdShow);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    return 0;
}
