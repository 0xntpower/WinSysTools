// Config.h -- TaskSchedulerView settings. The original persists its configuration
// in a .cfg INI file next to the executable (<exe>.cfg), section [General], not in
// the registry: the binary imports only the GetPrivateProfile*/WritePrivateProfile*
// APIs (its RegOpenKeyEx/RegQueryValueEx calls are the COM-handler CLSID lookups,
// read-only), path builder sub_1400119C8, value table sub_1400020FC. The /cfg switch
// overrides the file path. Value names match the original: MarkOddEvenRows,
// ShowGridLines, SaveFilterIndex, ShowInfoTip, ShowTimeInGMT, BlackBackground,
// HideDisabledTasks, LoadFrom, ComputerName, AutoStartRemoteRegistry,
// HideMicrosoftTasks, HideHiddenTasks, ShowOnlyRunningTasks, AddExportHeaderLine,
// AlwaysOnTop, AutoRefresh, AutoSizeColumnsOnLoad, SaveFileEncoding,
// AlignNumbersToRight, TrayIcon, Sort, WinPos, Columns.
#pragma once
#include "../common/CfgIni.h"
#include "Columns.h"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdlib>

// Legacy registry root the /deleteregkey switch removes (older versions stored
// settings here before the move to the .cfg file).
constexpr wchar_t kConfigKey[] = L"Software\\NirSoft\\TaskSchedulerView";

struct Config
{
    bool markOddEvenRows = false;
    bool showGridLines = false;
    bool showInfoTip = true;
    bool showTimeInGmt = false;
    bool blackBackground = false;
    bool hideDisabledTasks = false;
    bool hideMicrosoftTasks = false;
    bool hideHiddenTasks = false;
    bool showOnlyRunningTasks = false;
    bool addExportHeaderLine = true;
    bool alwaysOnTop = false;
    bool autoRefresh = false;
    bool autoSizeColumnsOnLoad = false;
    bool alignNumbersToRight = false;
    bool autoStartRemoteRegistry = false;
    bool trayIcon = false;

    int  loadFrom = 1;                 // 1 This computer, 2 Remote computer
    std::wstring computerName;

    int  saveFileEncoding = 0;         // 0 Default, 1 ANSI, 2 UTF-16, 3 UTF-8
    int  saveFilterIndex = 1;

    int  sortColumn = 0;               // internal column index
    bool sortAscending = true;

    std::vector<int> visibleColumns;   // display order (internal indices)
    std::vector<int> columnWidths;     // per visible column

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring cfgPath = cfgini::DefaultPath();

    Config() { resetColumns(); }

    void resetColumns()
    {
        visibleColumns.assign(std::begin(kDefaultOrder), std::end(kDefaultOrder));
        columnWidths.clear();
        for (int c : visibleColumns) columnWidths.push_back(kColumns[c].width);
    }

    // /cfg command-line override: point the store at a different file.
    void setCfgPath(const std::wstring& arg) { cfgPath = cfgini::Resolve(arg); }

    void load()
    {
        const std::wstring& p = cfgPath;
        auto rb = [&](const wchar_t* n, bool& v) { v = cfgini::GetInt(p, n, v) != 0; };
        rb(L"MarkOddEvenRows", markOddEvenRows);
        rb(L"ShowGridLines", showGridLines);
        rb(L"ShowInfoTip", showInfoTip);
        rb(L"ShowTimeInGMT", showTimeInGmt);
        rb(L"BlackBackground", blackBackground);
        rb(L"HideDisabledTasks", hideDisabledTasks);
        rb(L"HideMicrosoftTasks", hideMicrosoftTasks);
        rb(L"HideHiddenTasks", hideHiddenTasks);
        rb(L"ShowOnlyRunningTasks", showOnlyRunningTasks);
        rb(L"AddExportHeaderLine", addExportHeaderLine);
        rb(L"AlwaysOnTop", alwaysOnTop);
        rb(L"AutoRefresh", autoRefresh);
        rb(L"AutoSizeColumnsOnLoad", autoSizeColumnsOnLoad);
        rb(L"AlignNumbersToRight", alignNumbersToRight);
        rb(L"AutoStartRemoteRegistry", autoStartRemoteRegistry);
        rb(L"TrayIcon", trayIcon);
        loadFrom = cfgini::GetInt(p, L"LoadFrom", loadFrom);
        saveFileEncoding = cfgini::GetInt(p, L"SaveFileEncoding", saveFileEncoding);
        saveFilterIndex = cfgini::GetInt(p, L"SaveFilterIndex", saveFilterIndex);
        computerName = cfgini::GetStr(p, L"ComputerName");

        const int s = cfgini::GetInt(p, L"Sort", 0);
        sortAscending = s >= 0;
        sortColumn = std::abs(s) < kColumnCount ? std::abs(s) : 0;

        loadColumns(p);

        auto wp = cfgini::GetBlob(p, L"WinPos");
        if (wp.size() >= sizeof(WINDOWPLACEMENT))
        {
            memcpy(&placement, wp.data(), sizeof(placement));
            placement.length = sizeof(placement);
            havePlacement = true;
        }
    }

    void save() const
    {
        const std::wstring& p = cfgPath;
        cfgini::PutInt(p, L"MarkOddEvenRows", markOddEvenRows);
        cfgini::PutInt(p, L"ShowGridLines", showGridLines);
        cfgini::PutInt(p, L"ShowInfoTip", showInfoTip);
        cfgini::PutInt(p, L"ShowTimeInGMT", showTimeInGmt);
        cfgini::PutInt(p, L"BlackBackground", blackBackground);
        cfgini::PutInt(p, L"HideDisabledTasks", hideDisabledTasks);
        cfgini::PutInt(p, L"HideMicrosoftTasks", hideMicrosoftTasks);
        cfgini::PutInt(p, L"HideHiddenTasks", hideHiddenTasks);
        cfgini::PutInt(p, L"ShowOnlyRunningTasks", showOnlyRunningTasks);
        cfgini::PutInt(p, L"AddExportHeaderLine", addExportHeaderLine);
        cfgini::PutInt(p, L"AlwaysOnTop", alwaysOnTop);
        cfgini::PutInt(p, L"AutoRefresh", autoRefresh);
        cfgini::PutInt(p, L"AutoSizeColumnsOnLoad", autoSizeColumnsOnLoad);
        cfgini::PutInt(p, L"AlignNumbersToRight", alignNumbersToRight);
        cfgini::PutInt(p, L"AutoStartRemoteRegistry", autoStartRemoteRegistry);
        cfgini::PutInt(p, L"TrayIcon", trayIcon);
        cfgini::PutInt(p, L"LoadFrom", loadFrom);
        cfgini::PutInt(p, L"SaveFileEncoding", saveFileEncoding);
        cfgini::PutInt(p, L"SaveFilterIndex", saveFilterIndex);
        cfgini::PutStr(p, L"ComputerName", computerName);
        cfgini::PutInt(p, L"Sort", sortAscending ? sortColumn : -sortColumn);
        saveColumns(p);
        if (havePlacement)
        {
            cfgini::PutBlob(p, L"WinPos", &placement, sizeof(placement));
        }
    }

private:
    // Columns blob: array of (WORD width, WORD columnIndex), display order.
    void loadColumns(const std::wstring& p)
    {
        auto blob = cfgini::GetBlob(p, L"Columns");
        if (blob.size() < 4)
        {
            return;
        }
        const size_t n = blob.size() / 4;
        std::vector<int> vis, wid;
        for (size_t i = 0; i < n; ++i)
        {
            const WORD w = static_cast<WORD>(blob[i * 4] | (blob[i * 4 + 1] << 8));
            const WORD c = static_cast<WORD>(blob[i * 4 + 2] | (blob[i * 4 + 3] << 8));
            if (c < kColumnCount)
            {
                vis.push_back(c);
                wid.push_back(w);
            }
        }
        if (!vis.empty())
        {
            visibleColumns = vis;
            columnWidths = wid;
        }
    }

    void saveColumns(const std::wstring& p) const
    {
        std::vector<BYTE> blob;
        for (size_t i = 0; i < visibleColumns.size(); ++i)
        {
            const WORD w = static_cast<WORD>(i < columnWidths.size() ? columnWidths[i]
                                                                     : kColumns[visibleColumns[i]].width);
            const WORD c = static_cast<WORD>(visibleColumns[i]);
            blob.push_back(w & 0xFF);
            blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF);
            blob.push_back((c >> 8) & 0xFF);
        }
        if (!blob.empty())
        {
            cfgini::PutBlob(p, L"Columns", blob.data(), blob.size());
        }
    }
};
