// Config.h -- PreviousFilesRecovery settings. The original keeps no registry key, it
// reads and writes an INI file next to the executable (PreviousFilesRecovery.cfg, or
// the /cfg path). The value names in the [General] section match the original exactly
// (recovered from sub_140001F88 and sub_14000E7E0): MarkOddEvenRows, ShowGridLines,
// SaveFilterIndex, ShowInfoTip, ShowTimeInGMT, BaseFolder, FilesWildcard,
// SubfoldersDepth, FindPreviousFiles, FindDeletedFiles, FindDeletedFolders,
// MultipleCopiesMode, WinPos, Columns, Sort. The binary blobs are kept as hex strings.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include "Columns.h"

inline std::wstring DefaultConfigPath()
{
    wchar_t path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos)
    {
        p.resize(dot);
    }
    p += L".cfg";
    return p;
}

struct Config
{
    bool markOddEvenRows = false;
    bool showGridLines = false;
    bool showInfoTip = true;
    bool showTimeInGmt = false;

    std::wstring baseFolder;
    std::wstring filesWildcard = L"*";
    int subfoldersDepth = 0; // 0 = Unlimited
    bool findPreviousFiles = true;
    bool findDeletedFiles = true;
    bool findDeletedFolders = true;
    int multipleCopiesMode = 2; // 1 all, 2 different, 3 latest, 4 selected

    int saveFilterIndex = 0;

    int sortColumn = 0;
    bool sortAscending = true;

    std::vector<int> visibleColumns; // display order (internal indices)
    std::vector<int> columnWidths;   // per visible column

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path = DefaultConfigPath();

    Config()
    {
        resetColumns();
    }

    void resetColumns()
    {
        visibleColumns.assign(std::begin(kDefaultOrder), std::end(kDefaultOrder));
        columnWidths.clear();
        for (int c : visibleColumns)
        {
            columnWidths.push_back(kColumns[c].width);
        }
    }

    void load()
    {
        const wchar_t* f = path.c_str();
        auto gi = [&](const wchar_t* n, int def)
        {
            return (int)::GetPrivateProfileIntW(L"General", n, def, f);
        };
        auto gs = [&](const wchar_t* n, const wchar_t* def)
        {
            wchar_t buf[1024]{};
            ::GetPrivateProfileStringW(L"General", n, def, buf, 1024, f);
            return std::wstring(buf);
        };

        markOddEvenRows = gi(L"MarkOddEvenRows", 0) != 0;
        showGridLines = gi(L"ShowGridLines", 0) != 0;
        showInfoTip = gi(L"ShowInfoTip", 1) != 0;
        showTimeInGmt = gi(L"ShowTimeInGMT", 0) != 0;
        baseFolder = gs(L"BaseFolder", L"");
        filesWildcard = gs(L"FilesWildcard", L"*");
        subfoldersDepth = gi(L"SubfoldersDepth", 0);
        findPreviousFiles = gi(L"FindPreviousFiles", 1) != 0;
        findDeletedFiles = gi(L"FindDeletedFiles", 1) != 0;
        findDeletedFolders = gi(L"FindDeletedFolders", 1) != 0;
        multipleCopiesMode = gi(L"MultipleCopiesMode", 2);
        saveFilterIndex = gi(L"SaveFilterIndex", 0);

        int s = gi(L"Sort", 0);
        sortAscending = s >= 0;
        sortColumn = std::abs(s) < kColumnCount ? std::abs(s) : 0;

        loadColumns(gs(L"Columns", L""));
        loadPlacement(gs(L"WinPos", L""));
    }

    void save() const
    {
        const wchar_t* f = path.c_str();
        auto si = [&](const wchar_t* n, int v)
        {
            ::WritePrivateProfileStringW(L"General", n, std::to_wstring(v).c_str(), f);
        };

        si(L"MarkOddEvenRows", markOddEvenRows);
        si(L"ShowGridLines", showGridLines);
        si(L"ShowInfoTip", showInfoTip);
        si(L"ShowTimeInGMT", showTimeInGmt);
        ::WritePrivateProfileStringW(L"General", L"BaseFolder", baseFolder.c_str(), f);
        ::WritePrivateProfileStringW(L"General", L"FilesWildcard", filesWildcard.c_str(), f);
        si(L"SubfoldersDepth", subfoldersDepth);
        si(L"FindPreviousFiles", findPreviousFiles);
        si(L"FindDeletedFiles", findDeletedFiles);
        si(L"FindDeletedFolders", findDeletedFolders);
        si(L"MultipleCopiesMode", multipleCopiesMode);
        si(L"SaveFilterIndex", saveFilterIndex);
        si(L"Sort", sortAscending ? sortColumn : -sortColumn);

        ::WritePrivateProfileStringW(L"General", L"Columns", saveColumns().c_str(), f);
        if (havePlacement)
        {
            ::WritePrivateProfileStringW(L"General", L"WinPos", toHex(&placement, sizeof(placement)).c_str(), f);
        }
    }

    void deleteConfig() const
    {
        ::DeleteFileW(path.c_str());
    }

  private:
    static std::wstring toHex(const void* data, size_t n)
    {
        static const wchar_t* d = L"0123456789ABCDEF";
        const BYTE* p = (const BYTE*)data;
        std::wstring s;
        for (size_t i = 0; i < n; ++i)
        {
            s += d[p[i] >> 4];
            s += d[p[i] & 0xF];
        }
        return s;
    }
    static std::vector<BYTE> fromHex(const std::wstring& s)
    {
        std::vector<BYTE> v;
        auto nib = [](wchar_t c) -> int
        {
            if (c >= L'0' && c <= L'9')
            {
                return c - L'0';
            }
            if (c >= L'A' && c <= L'F')
            {
                return c - L'A' + 10;
            }
            if (c >= L'a' && c <= L'f')
            {
                return c - L'a' + 10;
            }
            return -1;
        };
        for (size_t i = 0; i + 1 < s.size(); i += 2)
        {
            int hi = nib(s[i]), lo = nib(s[i + 1]);
            if (hi < 0 || lo < 0)
            {
                break;
            }
            v.push_back((BYTE)((hi << 4) | lo));
        }
        return v;
    }

    // Columns blob: array of (WORD width, WORD columnIndex) in display order.
    std::wstring saveColumns() const
    {
        std::vector<BYTE> blob;
        for (size_t i = 0; i < visibleColumns.size(); ++i)
        {
            WORD w = (WORD)(i < columnWidths.size() ? columnWidths[i] : kColumns[visibleColumns[i]].width);
            WORD c = (WORD)visibleColumns[i];
            blob.push_back(w & 0xFF);
            blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF);
            blob.push_back((c >> 8) & 0xFF);
        }
        return toHex(blob.data(), blob.size());
    }
    void loadColumns(const std::wstring& hex)
    {
        auto blob = fromHex(hex);
        if (blob.size() < 4)
        {
            return;
        }
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 3 < blob.size(); i += 4)
        {
            WORD w = blob[i] | (blob[i + 1] << 8);
            WORD c = blob[i + 2] | (blob[i + 3] << 8);
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
    void loadPlacement(const std::wstring& hex)
    {
        auto blob = fromHex(hex);
        if (blob.size() >= sizeof(WINDOWPLACEMENT))
        {
            memcpy(&placement, blob.data(), sizeof(placement));
            placement.length = sizeof(placement);
            havePlacement = true;
        }
    }
};
