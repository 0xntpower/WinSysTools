// Config.h -- ShadowCopyView settings. The original keeps no registry key. It reads
// and writes a ShadowCopyView.cfg INI file next to the executable, section [General],
// so this does the same. Value names match the binary: MarkOddEvenRows, ShowGridLines,
// ShowInfoTip, ShowTimeInGMT, SaveFilterIndex, VerSplitLoc, WinPos, Sort, Columns,
// FilesSort, FilesColumns. The binary blobs WinPos, Columns and FilesColumns are
// stored as hex strings, the way the shared NirSoft config layer encodes them.
#pragma once
#include "Columns.h"
#include <windows.h>
#include <string>
#include <vector>

struct ColumnState
{
    std::vector<int> visible;     // display order, internal indices
    std::vector<int> widths;      // per visible column
    int  sortColumn = 0;
    bool sortAscending = true;

    void reset(const ColumnDef* defs, int count)
    {
        visible.clear();
        widths.clear();
        for (int i = 0; i < count; ++i) { visible.push_back(i); widths.push_back(defs[i].width); }
        sortColumn = 0;
        sortAscending = true;
    }
};

struct Config
{
    bool markOddEvenRows = false;
    bool showGridLines = false;
    bool showInfoTip = true;
    bool showTimeInGmt = false;
    int  saveFilterIndex = 1;
    int  verSplitLoc = 0;          // snapshot pane height in pixels, 0 means default

    ColumnState snapCols;
    ColumnState fileCols;

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path;             // the .cfg file, resolved once

    Config()
    {
        snapCols.reset(kSnapColumns, kSnapColumnCount);
        fileCols.reset(kFileColumns, kFileColumnCount);
    }

    static std::wstring DefaultPath()
    {
        wchar_t exe[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        size_t dot = p.rfind(L'.');
        if (dot != std::wstring::npos) p.resize(dot);
        return p + L".cfg";
    }

    void load()
    {
        if (path.empty()) path = DefaultPath();
        const wchar_t* f = path.c_str();
        auto gi = [&](const wchar_t* n, int def) { return (int)::GetPrivateProfileIntW(L"General", n, def, f); };

        markOddEvenRows = gi(L"MarkOddEvenRows", 0) != 0;
        showGridLines   = gi(L"ShowGridLines", 0) != 0;
        showInfoTip     = gi(L"ShowInfoTip", 1) != 0;
        showTimeInGmt   = gi(L"ShowTimeInGMT", 0) != 0;
        saveFilterIndex = gi(L"SaveFilterIndex", 1);
        verSplitLoc     = gi(L"VerSplitLoc", 0);

        loadColumns(L"Columns", L"Sort", snapCols, kSnapColumns, kSnapColumnCount);
        loadColumns(L"FilesColumns", L"FilesSort", fileCols, kFileColumns, kFileColumnCount);

        std::vector<BYTE> wp;
        if (readBlob(L"WinPos", wp) && wp.size() >= sizeof(WINDOWPLACEMENT))
        {
            memcpy(&placement, wp.data(), sizeof(placement));
            placement.length = sizeof(placement);
            havePlacement = true;
        }
    }

    void save() const
    {
        if (path.empty()) return;
        const wchar_t* f = path.c_str();
        auto si = [&](const wchar_t* n, int v) {
            wchar_t b[16]; swprintf(b, 16, L"%d", v);
            ::WritePrivateProfileStringW(L"General", n, b, f);
        };
        si(L"MarkOddEvenRows", markOddEvenRows);
        si(L"ShowGridLines", showGridLines);
        si(L"ShowInfoTip", showInfoTip);
        si(L"ShowTimeInGMT", showTimeInGmt);
        si(L"SaveFilterIndex", saveFilterIndex);
        si(L"VerSplitLoc", verSplitLoc);

        saveColumns(L"Columns", L"Sort", snapCols);
        saveColumns(L"FilesColumns", L"FilesSort", fileCols);

        if (havePlacement)
            writeBlob(L"WinPos", &placement, sizeof(placement));
    }

private:
    static std::wstring toHex(const void* data, size_t bytes)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        s.reserve(bytes * 2);
        static const wchar_t* hx = L"0123456789ABCDEF";
        for (size_t i = 0; i < bytes; ++i) { s += hx[p[i] >> 4]; s += hx[p[i] & 0xF]; }
        return s;
    }
    static std::vector<BYTE> fromHex(const std::wstring& s)
    {
        std::vector<BYTE> out;
        auto nib = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            return -1;
        };
        for (size_t i = 0; i + 1 < s.size(); i += 2)
        {
            int hi = nib(s[i]), lo = nib(s[i + 1]);
            if (hi < 0 || lo < 0) break;
            out.push_back(static_cast<BYTE>((hi << 4) | lo));
        }
        return out;
    }

    bool readBlob(const wchar_t* name, std::vector<BYTE>& out) const
    {
        wchar_t buf[4096]{};
        ::GetPrivateProfileStringW(L"General", name, L"", buf, 4096, path.c_str());
        if (buf[0] == 0) return false;
        out = fromHex(buf);
        return !out.empty();
    }
    void writeBlob(const wchar_t* name, const void* data, size_t bytes) const
    {
        std::wstring hex = toHex(data, bytes);
        ::WritePrivateProfileStringW(L"General", name, hex.c_str(), path.c_str());
    }

    void loadColumns(const wchar_t* colName, const wchar_t* sortName,
                     ColumnState& st, const ColumnDef* defs, int count)
    {
        int s = (int)::GetPrivateProfileIntW(L"General", sortName, 0, path.c_str());
        st.sortAscending = s >= 0;
        st.sortColumn = (std::abs(s) < count) ? std::abs(s) : 0;

        std::vector<BYTE> blob;
        if (!readBlob(colName, blob) || blob.size() < 4) return;
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 3 < blob.size(); i += 4)
        {
            WORD w = blob[i] | (blob[i + 1] << 8);
            WORD c = blob[i + 2] | (blob[i + 3] << 8);
            if (c < count) { vis.push_back(c); wid.push_back(w); }
        }
        if (!vis.empty()) { st.visible = vis; st.widths = wid; }
    }

    void saveColumns(const wchar_t* colName, const wchar_t* sortName, const ColumnState& st) const
    {
        wchar_t sb[16];
        swprintf(sb, 16, L"%d", st.sortAscending ? st.sortColumn : -st.sortColumn);
        ::WritePrivateProfileStringW(L"General", sortName, sb, path.c_str());

        std::vector<BYTE> blob;
        for (size_t i = 0; i < st.visible.size(); ++i)
        {
            WORD w = (WORD)(i < st.widths.size() ? st.widths[i] : 100);
            WORD c = (WORD)st.visible[i];
            blob.push_back(w & 0xFF); blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF); blob.push_back((c >> 8) & 0xFF);
        }
        if (!blob.empty()) writeBlob(colName, blob.data(), blob.size());
    }
};
