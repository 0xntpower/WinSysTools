// Config.h -- WinUpdatesView settings. The original persists its configuration in
// a .cfg INI file next to the executable (<exe>.cfg), section [General], not in the
// registry: the binary imports only the GetPrivateProfile*/WritePrivateProfile*
// APIs and no registry value writer (path builder sub_140010D38, value table
// sub_1400021DC). The /cfg switch overrides the file path. Value names and defaults
// match the original: ShowTimeInGMT, HideItemsCompatibility, HideWindowsDefender,
// MarkOddEvenRows, ShowGridLines, SaveFilterIndex, ShowInfoTip, LoadFrom,
// StopWinUpdatesService, ExternalFilename, ComputerName, AddExportHeaderLine, Sort,
// WinPos, Columns. Blobs are the original uppercase space-separated hex format.
#pragma once
#include "../common/CfgIni.h"
#include "Columns.h"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdlib>

struct Config
{
    bool showTimeInGmt = false;
    bool hideItemsCompatibility = false;
    bool hideWindowsDefender = false;
    bool markOddEvenRows = false;
    bool showGridLines = false;
    bool showInfoTip = true;
    bool addExportHeaderLine = true;
    bool stopWinUpdatesService = true;

    int  loadFrom = 1;               // 1 API, 2 local DB, 3 external DB, 4 remote
    std::wstring externalFilename;
    std::wstring computerName;

    int  sortColumn = 0;             // sort column index
    bool sortAscending = true;

    // visible columns (display order) plus their widths, defaults from Columns.h
    std::vector<int> visibleColumns;
    std::vector<int> columnWidths;

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring cfgPath = cfgini::DefaultPath();

    Config() { resetColumns(); }

    void resetColumns()
    {
        visibleColumns.clear();
        columnWidths.clear();
        for (int i = 0; i < kColumnCount; ++i)
        {
            visibleColumns.push_back(i);
            columnWidths.push_back(kColumns[i].width);
        }
    }

    // /cfg command-line override: point the store at a different file.
    void setCfgPath(const std::wstring& arg) { cfgPath = cfgini::Resolve(arg); }

    void load()
    {
        const std::wstring& p = cfgPath;
        showTimeInGmt = cfgini::GetInt(p, L"ShowTimeInGMT", showTimeInGmt) != 0;
        hideItemsCompatibility = cfgini::GetInt(p, L"HideItemsCompatibility", hideItemsCompatibility) != 0;
        hideWindowsDefender = cfgini::GetInt(p, L"HideWindowsDefender", hideWindowsDefender) != 0;
        markOddEvenRows = cfgini::GetInt(p, L"MarkOddEvenRows", markOddEvenRows) != 0;
        showGridLines = cfgini::GetInt(p, L"ShowGridLines", showGridLines) != 0;
        showInfoTip = cfgini::GetInt(p, L"ShowInfoTip", showInfoTip) != 0;
        addExportHeaderLine = cfgini::GetInt(p, L"AddExportHeaderLine", addExportHeaderLine) != 0;
        stopWinUpdatesService = cfgini::GetInt(p, L"StopWinUpdatesService", stopWinUpdatesService) != 0;
        loadFrom = cfgini::GetInt(p, L"LoadFrom", loadFrom);
        externalFilename = cfgini::GetStr(p, L"ExternalFilename");
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
        cfgini::PutInt(p, L"ShowTimeInGMT", showTimeInGmt);
        cfgini::PutInt(p, L"HideItemsCompatibility", hideItemsCompatibility);
        cfgini::PutInt(p, L"HideWindowsDefender", hideWindowsDefender);
        cfgini::PutInt(p, L"MarkOddEvenRows", markOddEvenRows);
        cfgini::PutInt(p, L"ShowGridLines", showGridLines);
        cfgini::PutInt(p, L"ShowInfoTip", showInfoTip);
        cfgini::PutInt(p, L"AddExportHeaderLine", addExportHeaderLine);
        cfgini::PutInt(p, L"StopWinUpdatesService", stopWinUpdatesService);
        cfgini::PutInt(p, L"LoadFrom", loadFrom);
        cfgini::PutStr(p, L"ExternalFilename", externalFilename);
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
