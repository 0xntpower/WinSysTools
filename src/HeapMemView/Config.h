// Config.h -- HeapMemView.cfg, the INI settings store next to the executable.
//
// Section [General]. Every value name and default below is recovered from the
// original load/save sub_14000B114 and sub_140001B90. WinPos is a WINDOWPLACEMENT
// written as space-free hex bytes, Columns is a blob of (width, columnIndex) pairs
// in little-endian 16-bit fields, one pair per visible column, matching the layout
// the original stores. The legacy registry root Software\0xntpower\HeapMemView is
// removed by the /deleteregkey switch.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Columns.h"

struct Settings
{
    bool     showGridLines        = false;        // ShowGridLines
    int      saveFilterIndex      = 0;            // SaveFilterIndex
    int      verSplitLoc          = 0x3FFF;       // VerSplitLoc (upper pane height, 0x3FFF = default split)
    int      bytesPerLine         = 16;           // BytesPerLine (lower pane)
    bool     displayMemHeader     = false;        // DisplayMemHeader
    COLORREF newMarkColor         = 0xE0FFFF;     // NewMemoryBlocksMarkColor (light pink)
    bool     markNew              = true;         // MarkNewMemoryBlocks
    int      filterFromSize       = 80;           // Filter.FromSize
    int      filterToSize         = 10000;        // Filter.ToSize
    bool     filterUseFrom        = true;         // Filter.UseFromSize
    bool     filterUseTo          = false;        // Filter.UseToSize
    std::wstring defaultExportExt  = L"bin";      // DefaultExportExt
    bool     displayAbove127      = false;        // DisplayAboveAscii127
    int      dataPreviewBytes     = 32;           // DataPreviewBytes
    int      dataPreviewMode      = 0;            // DataPreviewMode 0=none 1=ascii 2=hex
    int      dataPreviewStartByte = 0;            // DataPreviewStartByte
    int      selectProcessSort    = 0;            // SelectProcessSort
    std::wstring lastProcess       = L"";         // LastProcess
    bool     displayMemoryAddress = true;         // DisplayMemoryAddress (lower pane address column)
    bool     autoSortOnRefresh    = false;        // AutoSortOnRefresh
    int      sortColumn           = 0;            // Sort

    std::vector<int> visible;                     // Columns blob -> visible column order
    int              width[kColumnCount];         // per-column width
    bool             havePlacement = false;
    WINDOWPLACEMENT  placement{};

    Settings()
    {
        for (int i = 0; i < kColumnCount; ++i) { visible.push_back(i); width[i] = kColumns[i].width; }
    }
};

inline std::wstring ConfigPath()
{
    wchar_t exe[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    size_t s = p.find_last_of(L"\\/");
    if (s != std::wstring::npos) p.resize(s + 1);
    return p + L"HeapMemView.cfg";
}

namespace cfgdetail
{
    // The original stores blob values as uppercase hex bytes separated by a
    // single space ("%2.2X "), read back by pairing non-space hex characters
    // (sub_14000DC88 / sub_1400044D8). Match that on-disk format exactly.
    inline std::vector<BYTE> parseHexBytes(const std::wstring& s)
    {
        std::vector<BYTE> out;
        wchar_t pair[3] = {};
        int have = 0;
        for (wchar_t c : s)
        {
            if (c == L' ')
                continue;
            pair[have++] = c;
            if (have == 2)
            {
                out.push_back(static_cast<BYTE>(wcstoul(pair, nullptr, 16)));
                have = 0;
            }
        }
        return out;
    }
    inline std::wstring hexBytes(const void* data, size_t n)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        wchar_t b[8] = {};
        for (size_t i = 0; i < n; ++i) { swprintf(b, 8, L"%2.2X ", p[i]); s += b; }
        return s;
    }
    inline int getInt(const wchar_t* key, int def, const std::wstring& path)
    {
        return static_cast<int>(::GetPrivateProfileIntW(L"General", key, def, path.c_str()));
    }
    inline std::wstring getStr(const wchar_t* key, const std::wstring& path)
    {
        wchar_t buf[4096] = {};
        ::GetPrivateProfileStringW(L"General", key, L"", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        return buf;
    }
    inline void putInt(const wchar_t* key, int v, const std::wstring& path)
    {
        wchar_t b[16] = {}; swprintf(b, 16, L"%d", v);
        ::WritePrivateProfileStringW(L"General", key, b, path.c_str());
    }
    inline void putStr(const wchar_t* key, const std::wstring& v, const std::wstring& path)
    {
        ::WritePrivateProfileStringW(L"General", key, v.c_str(), path.c_str());
    }
}

inline void LoadSettings(Settings& s, const std::wstring& path)
{
    using namespace cfgdetail;
    s.showGridLines        = getInt(L"ShowGridLines", 0, path) != 0;
    s.saveFilterIndex      = getInt(L"SaveFilterIndex", 0, path);
    s.verSplitLoc          = getInt(L"VerSplitLoc", 0x3FFF, path);
    s.bytesPerLine         = getInt(L"BytesPerLine", 16, path);
    s.displayMemHeader     = getInt(L"DisplayMemHeader", 0, path) != 0;
    s.newMarkColor         = static_cast<COLORREF>(getInt(L"NewMemoryBlocksMarkColor", 0xE0FFFF, path));
    s.markNew              = getInt(L"MarkNewMemoryBlocks", 1, path) != 0;
    s.filterFromSize       = getInt(L"Filter.FromSize", 80, path);
    s.filterToSize         = getInt(L"Filter.ToSize", 10000, path);
    s.filterUseFrom        = getInt(L"Filter.UseFromSize", 1, path) != 0;
    s.filterUseTo          = getInt(L"Filter.UseToSize", 0, path) != 0;
    s.displayAbove127      = getInt(L"DisplayAboveAscii127", 0, path) != 0;
    s.dataPreviewBytes     = getInt(L"DataPreviewBytes", 32, path);
    s.dataPreviewMode      = getInt(L"DataPreviewMode", 0, path);
    s.dataPreviewStartByte = getInt(L"DataPreviewStartByte", 0, path);
    s.selectProcessSort    = getInt(L"SelectProcessSort", 0, path);
    s.displayMemoryAddress = getInt(L"DisplayMemoryAddress", 1, path) != 0;
    s.autoSortOnRefresh    = getInt(L"AutoSortOnRefresh", 0, path) != 0;
    s.sortColumn           = getInt(L"Sort", 0, path);
    if (s.sortColumn < 0 || s.sortColumn >= kColumnCount) s.sortColumn = 0;

    std::wstring ext = getStr(L"DefaultExportExt", path);
    if (!ext.empty()) s.defaultExportExt = ext;
    s.lastProcess = getStr(L"LastProcess", path);

    auto wp = parseHexBytes(getStr(L"WinPos", path));
    if (wp.size() >= sizeof(WINDOWPLACEMENT))
    {
        memcpy(&s.placement, wp.data(), sizeof(WINDOWPLACEMENT));
        s.placement.length = sizeof(WINDOWPLACEMENT);
        s.havePlacement = true;
    }

    auto cols = parseHexBytes(getStr(L"Columns", path));
    if (cols.size() >= 4)
    {
        std::vector<int> vis;
        for (size_t i = 0; i + 4 <= cols.size(); i += 4)
        {
            int w   = cols[i] | (cols[i + 1] << 8);
            int idx = cols[i + 2] | (cols[i + 3] << 8);
            if (idx >= 0 && idx < kColumnCount)
            {
                vis.push_back(idx);
                if (w > 0) s.width[idx] = w;
            }
        }
        if (!vis.empty()) s.visible = vis;
    }
}

inline void SaveSettings(const Settings& s, const std::wstring& path, const std::vector<int>& curWidths)
{
    using namespace cfgdetail;
    putInt(L"ShowGridLines", s.showGridLines, path);
    putInt(L"SaveFilterIndex", s.saveFilterIndex, path);
    putInt(L"VerSplitLoc", s.verSplitLoc, path);
    putInt(L"BytesPerLine", s.bytesPerLine, path);
    putInt(L"DisplayMemHeader", s.displayMemHeader, path);
    putInt(L"NewMemoryBlocksMarkColor", static_cast<int>(s.newMarkColor), path);
    putInt(L"MarkNewMemoryBlocks", s.markNew, path);
    putInt(L"Filter.FromSize", s.filterFromSize, path);
    putInt(L"Filter.ToSize", s.filterToSize, path);
    putInt(L"Filter.UseFromSize", s.filterUseFrom, path);
    putInt(L"Filter.UseToSize", s.filterUseTo, path);
    putStr(L"DefaultExportExt", s.defaultExportExt, path);
    putInt(L"DisplayAboveAscii127", s.displayAbove127, path);
    putInt(L"DataPreviewBytes", s.dataPreviewBytes, path);
    putInt(L"DataPreviewMode", s.dataPreviewMode, path);
    putInt(L"DataPreviewStartByte", s.dataPreviewStartByte, path);
    putInt(L"SelectProcessSort", s.selectProcessSort, path);
    putStr(L"LastProcess", s.lastProcess, path);
    putInt(L"DisplayMemoryAddress", s.displayMemoryAddress, path);
    putInt(L"AutoSortOnRefresh", s.autoSortOnRefresh, path);
    putInt(L"Sort", s.sortColumn, path);

    if (s.havePlacement)
        putStr(L"WinPos", hexBytes(&s.placement, sizeof(WINDOWPLACEMENT)), path);

    std::vector<BYTE> blob;
    for (size_t i = 0; i < s.visible.size(); ++i)
    {
        int c = s.visible[i];
        int w = (i < curWidths.size()) ? curWidths[i] : s.width[c];
        blob.push_back(w & 0xFF);  blob.push_back((w >> 8) & 0xFF);
        blob.push_back(c & 0xFF);  blob.push_back((c >> 8) & 0xFF);
    }
    putStr(L"Columns", hexBytes(blob.data(), blob.size()), path);
}
