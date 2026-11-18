// Config.h -- CredentialsFileView settings. The original keeps no registry config key.
// It reads and writes a CredentialsFileView.cfg INI file next to the executable, section
// [General]. Confirmed from the binary: the save routine (sub_14001A9A8) runs against a
// config object that sub_140007E14 builds from the module path with the extension stripped
// and .cfg appended, writing through WritePrivateProfileStringW under the General section.
// Value names and defaults recovered from the binary: MarkOddEvenRows, ShowGridLines,
// SaveFilterIndex, ShowInfoTip, VerSplitLoc, LowerPaneMode, ShowTimeInGMT, SaveFileEncoding,
// DataSource, UserName, RegHiveFolder, ProtectFolder1/2, CredentialsFolders1/2/3, Sort,
// Columns, WinPos. The binary blobs WinPos and Columns are kept as hex strings, the way the
// shared config layer encodes them. The login password is never persisted.
#pragma once
#include "Columns.h"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdlib>

struct Config
{
    bool markOddEvenRows = false;
    bool showGridLines = false;
    bool showInfoTip = true;
    bool showTimeInGmt = false;

    int saveFileEncoding = 0; // 0 Default, 1 ANSI, 2 UTF-16, 3 UTF-8
    int saveFilterIndex = 0;
    int lowerPaneMode = 1;    // 1 Full Hex Dump, 2 Strings Only
    int verSplitLoc = 0x3FFF; // vertical splitter position

    int dataSource = 2; // 1 current user, 2 any system, 3 another user, 4 all users
    std::wstring userName;
    std::wstring password_; // login password, transient only, never persisted

    std::wstring regHiveFolder;
    std::wstring protectFolder1, protectFolder2;
    std::wstring credFolder1, credFolder2, credFolder3;

    int sortColumn = 0;
    bool sortAscending = true;

    std::vector<int> visibleColumns;
    std::vector<int> columnWidths;

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path; // the .cfg file, resolved once

    Config()
    {
        resetColumns();
    }

    [[nodiscard]] static std::wstring DefaultPath()
    {
        wchar_t exe[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        size_t dot = p.rfind(L'.');
        if (dot != std::wstring::npos)
        {
            p.resize(dot);
        }
        return p + L".cfg";
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
        if (path.empty())
        {
            path = DefaultPath();
        }
        const wchar_t* f = path.c_str();
        auto gi = [&](const wchar_t* n, int def)
        {
            return static_cast<int>(::GetPrivateProfileIntW(L"General", n, def, f));
        };
        auto gs = [&](const wchar_t* n)
        {
            wchar_t buf[1024]{};
            ::GetPrivateProfileStringW(L"General", n, L"", buf, 1024, f);
            return std::wstring(buf);
        };

        markOddEvenRows = gi(L"MarkOddEvenRows", markOddEvenRows) != 0;
        showGridLines = gi(L"ShowGridLines", showGridLines) != 0;
        showInfoTip = gi(L"ShowInfoTip", showInfoTip) != 0;
        showTimeInGmt = gi(L"ShowTimeInGMT", showTimeInGmt) != 0;
        saveFileEncoding = gi(L"SaveFileEncoding", saveFileEncoding);
        saveFilterIndex = gi(L"SaveFilterIndex", saveFilterIndex);
        lowerPaneMode = gi(L"LowerPaneMode", lowerPaneMode);
        verSplitLoc = gi(L"VerSplitLoc", verSplitLoc);
        dataSource = gi(L"DataSource", dataSource);

        if (std::wstring s = gs(L"UserName"); !s.empty())
        {
            userName = s;
        }
        if (std::wstring s = gs(L"RegHiveFolder"); !s.empty())
        {
            regHiveFolder = s;
        }
        if (std::wstring s = gs(L"ProtectFolder1"); !s.empty())
        {
            protectFolder1 = s;
        }
        if (std::wstring s = gs(L"ProtectFolder2"); !s.empty())
        {
            protectFolder2 = s;
        }
        if (std::wstring s = gs(L"CredentialsFolders1"); !s.empty())
        {
            credFolder1 = s;
        }
        if (std::wstring s = gs(L"CredentialsFolders2"); !s.empty())
        {
            credFolder2 = s;
        }
        if (std::wstring s = gs(L"CredentialsFolders3"); !s.empty())
        {
            credFolder3 = s;
        }

        int s = gi(L"Sort", sortAscending ? sortColumn : -sortColumn);
        sortAscending = s >= 0;
        sortColumn = std::abs(s) < kColumnCount ? std::abs(s) : 0;

        loadColumns(gs(L"Columns"));
        loadPlacement(gs(L"WinPos"));
    }

    // The /cfg command-line option points the store at another INI path.
    void loadIni(const std::wstring& p)
    {
        path = p;
        load();
    }

    void save() const
    {
        const std::wstring p = path.empty() ? DefaultPath() : path;
        const wchar_t* f = p.c_str();
        auto si = [&](const wchar_t* n, int v)
        {
            wchar_t b[16]{};
            swprintf(b, 16, L"%d", v);
            ::WritePrivateProfileStringW(L"General", n, b, f);
        };
        auto ss = [&](const wchar_t* n, const std::wstring& v)
        {
            ::WritePrivateProfileStringW(L"General", n, v.c_str(), f);
        };

        si(L"MarkOddEvenRows", markOddEvenRows);
        si(L"ShowGridLines", showGridLines);
        si(L"ShowInfoTip", showInfoTip);
        si(L"ShowTimeInGMT", showTimeInGmt);
        si(L"SaveFileEncoding", saveFileEncoding);
        si(L"SaveFilterIndex", saveFilterIndex);
        si(L"LowerPaneMode", lowerPaneMode);
        si(L"VerSplitLoc", verSplitLoc);
        si(L"DataSource", dataSource);
        ss(L"UserName", userName);
        ss(L"RegHiveFolder", regHiveFolder);
        ss(L"ProtectFolder1", protectFolder1);
        ss(L"ProtectFolder2", protectFolder2);
        ss(L"CredentialsFolders1", credFolder1);
        ss(L"CredentialsFolders2", credFolder2);
        ss(L"CredentialsFolders3", credFolder3);
        si(L"Sort", sortAscending ? sortColumn : -sortColumn);

        ss(L"Columns", saveColumns());
        if (havePlacement)
        {
            ss(L"WinPos", toHex(&placement, sizeof(placement)));
        }
    }

    void deleteConfig() const
    {
        ::DeleteFileW((path.empty() ? DefaultPath() : path).c_str());
    }

    [[nodiscard]] std::vector<std::wstring> folders() const
    {
        std::vector<std::wstring> v;
        for (const std::wstring* fld : {&credFolder1, &credFolder2, &credFolder3})
        {
            if (!fld->empty())
            {
                v.push_back(*fld);
            }
        }
        return v;
    }

  private:
    // The INI layer encodes a blob as space-separated uppercase hex bytes ("%02X ").
    [[nodiscard]] static std::wstring toHex(const void* data, size_t bytes)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        s.reserve(bytes * 3);
        static const wchar_t* hx = L"0123456789ABCDEF";
        for (size_t i = 0; i < bytes; ++i)
        {
            if (i)
            {
                s += L' ';
            }
            s += hx[p[i] >> 4];
            s += hx[p[i] & 0xF];
        }
        return s;
    }
    [[nodiscard]] static std::vector<BYTE> fromHex(const std::wstring& s)
    {
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
        std::vector<BYTE> out;
        int hi = -1;
        for (wchar_t c : s) // tolerates spaced or continuous hex
        {
            int v = nib(c);
            if (v < 0)
            {
                continue;
            }
            if (hi < 0)
            {
                hi = v;
            }
            else
            {
                out.push_back(static_cast<BYTE>((hi << 4) | v));
                hi = -1;
            }
        }
        return out;
    }

    // Columns blob: array of (WORD width, WORD columnIndex), display order.
    [[nodiscard]] std::wstring saveColumns() const
    {
        std::vector<BYTE> blob;
        for (size_t i = 0; i < visibleColumns.size(); ++i)
        {
            WORD w = static_cast<WORD>(i < columnWidths.size() ? columnWidths[i] : kColumns[visibleColumns[i]].width);
            WORD c = static_cast<WORD>(visibleColumns[i]);
            blob.push_back(w & 0xFF);
            blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF);
            blob.push_back((c >> 8) & 0xFF);
        }
        return toHex(blob.data(), blob.size());
    }
    void loadColumns(const std::wstring& hex)
    {
        std::vector<BYTE> blob = fromHex(hex);
        if (blob.size() < 4)
        {
            return;
        }
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 3 < blob.size(); i += 4)
        {
            WORD w = static_cast<WORD>(blob[i] | (blob[i + 1] << 8));
            WORD c = static_cast<WORD>(blob[i + 2] | (blob[i + 3] << 8));
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
        std::vector<BYTE> blob = fromHex(hex);
        if (blob.size() >= sizeof(WINDOWPLACEMENT))
        {
            memcpy(&placement, blob.data(), sizeof(placement));
            placement.length = sizeof(placement);
            havePlacement = true;
        }
    }
};
