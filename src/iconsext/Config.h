// Config.h -- the iconsext.cfg settings file (INI next to the executable), the
// store the original moved to in v1.42. Section and value names are taken verbatim
// from the shipped iconsext.cfg. WinPos and the Columns blob are kept as the
// original's space-separated uppercase hex-byte strings.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include "ResourceScan.h"
#include "Columns.h"

struct AppConfig
{
    // [General]
    std::vector<BYTE> winPos;                  // WinPos (WINDOWPLACEMENT bytes)
    int               columnWidths[kColumnCount] = { 150, 70, 120, 60, 100, 300 };
    int               lvMode = 0;              // LVMode (0 large,1 small,2 list,3 details)
    int               sort = 0;                // Sort1
    std::wstring      saveFolder;              // Save.Folder
    bool              saveCursorsAsIco = false;// Save.CursorsAsico
    std::wstring      wildcard = L"C:\\WINDOWS\\system32\\shell32.dll"; // Search.Wildcard
    ScanOptions       search;                  // Search.*
    int               scanMode = 1;            // Search.ScanMode (1 files, 2 process)

    std::wstring path() const
    {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring p = buf;
        if (auto dot = p.find_last_of(L'.'); dot != std::wstring::npos) p.erase(dot);
        return p + L".cfg";
    }

    static std::wstring toHex(const std::vector<BYTE>& bytes)
    {
        std::wstring out;
        wchar_t b[4];
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            swprintf(b, 4, L"%02X", bytes[i]);
            if (i) out.push_back(L' ');
            out += b;
        }
        return out;
    }
    static std::vector<BYTE> fromHex(const std::wstring& s)
    {
        std::vector<BYTE> out;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && s[i] == L' ') ++i;
            if (i >= s.size()) break;
            out.push_back(static_cast<BYTE>(wcstoul(s.c_str() + i, nullptr, 16)));
            while (i < s.size() && s[i] != L' ') ++i;
        }
        return out;
    }

    void Load()
    {
        const std::wstring f = path();
        const wchar_t* S = L"General";
        wchar_t buf[2048];

        GetPrivateProfileStringW(S, L"WinPos", L"", buf, 2048, f.c_str());
        winPos = fromHex(buf);

        GetPrivateProfileStringW(S, L"Columns", L"", buf, 2048, f.c_str());
        if (auto blob = fromHex(buf); blob.size() >= 4)
            for (size_t i = 0; i + 3 < blob.size(); i += 4)
            {
                int width = blob[i] | (blob[i + 1] << 8);
                int col = blob[i + 2] | (blob[i + 3] << 8);
                if (col >= 0 && col < kColumnCount) columnWidths[col] = width;
            }

        lvMode = GetPrivateProfileIntW(S, L"LVMode", 0, f.c_str());
        sort = GetPrivateProfileIntW(S, L"Sort1", 0, f.c_str());
        scanMode = GetPrivateProfileIntW(S, L"Search.ScanMode", 1, f.c_str());
        saveCursorsAsIco = GetPrivateProfileIntW(S, L"Save.CursorsAsico", 0, f.c_str()) != 0;

        GetPrivateProfileStringW(S, L"Save.Folder", L"", buf, 2048, f.c_str());
        saveFolder = buf;
        GetPrivateProfileStringW(S, L"Search.Wildcard", wildcard.c_str(), buf, 2048, f.c_str());
        wildcard = buf;

        search.icons = GetPrivateProfileIntW(S, L"Search.SearchIcons", 1, f.c_str()) != 0;
        search.cursors = GetPrivateProfileIntW(S, L"Search.SearchCursors", 1, f.c_str()) != 0;
        search.subdirs = GetPrivateProfileIntW(S, L"Search.SubDir", 0, f.c_str()) != 0;
        search.filterSize = GetPrivateProfileIntW(S, L"Search.FindWidthHeight", 0, f.c_str()) != 0;
        search.filterColor = GetPrivateProfileIntW(S, L"Search.FindColors", 0, f.c_str()) != 0;
        search.width = GetPrivateProfileIntW(S, L"Search.FindWidthValue", 32, f.c_str());
        search.height = GetPrivateProfileIntW(S, L"Search.FindHeightValue", 32, f.c_str());
        search.colorBits = GetPrivateProfileIntW(S, L"Search.FindColorsValue", 8, f.c_str());
    }

    void Save()
    {
        const std::wstring f = path();
        const wchar_t* S = L"General";
        auto setInt = [&](const wchar_t* k, int v) {
            WritePrivateProfileStringW(S, k, std::to_wstring(v).c_str(), f.c_str());
        };

        WritePrivateProfileStringW(S, L"WinPos", toHex(winPos).c_str(), f.c_str());

        std::vector<BYTE> blob;
        for (int c = 0; c < kColumnCount; ++c)
        {
            blob.push_back(columnWidths[c] & 0xFF);
            blob.push_back((columnWidths[c] >> 8) & 0xFF);
            blob.push_back(c & 0xFF);
            blob.push_back((c >> 8) & 0xFF);
        }
        WritePrivateProfileStringW(S, L"Columns", toHex(blob).c_str(), f.c_str());

        setInt(L"LVMode", lvMode);
        setInt(L"Sort1", sort);
        WritePrivateProfileStringW(S, L"Save.Folder", saveFolder.c_str(), f.c_str());
        setInt(L"Save.CursorsAsico", saveCursorsAsIco ? 1 : 0);
        WritePrivateProfileStringW(S, L"Search.Wildcard", wildcard.c_str(), f.c_str());
        setInt(L"Search.FindColors", search.filterColor ? 1 : 0);
        setInt(L"Search.FindWidthHeight", search.filterSize ? 1 : 0);
        setInt(L"Search.SearchCursors", search.cursors ? 1 : 0);
        setInt(L"Search.SearchIcons", search.icons ? 1 : 0);
        setInt(L"Search.SubDir", search.subdirs ? 1 : 0);
        setInt(L"Search.FindColorsValue", search.colorBits);
        setInt(L"Search.FindHeightValue", search.height);
        setInt(L"Search.FindWidthValue", search.width);
        setInt(L"Search.ScanMode", scanMode);
    }
};
