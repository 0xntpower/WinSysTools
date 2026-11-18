// Config.h -- SniffPass settings. The original keeps its settings in a SniffPass.cfg
// INI file next to the executable, section [General], since version 1.03. The value
// names match the binary and the shipped SniffPass.cfg: ShowGridLines, SaveFilterIndex,
// ExcludeDuplicates, BeepOnNewPassword, AddExportHeaderLine, AddToWindowsFirewall,
// CaptureMethod, the four adapter name values, WinPos, Columns and Sort. The WinPos and
// Columns blobs are stored as hex strings, the way the shared config layer does.
#pragma once
#include "Columns.h"
#include <windows.h>
#include <string>
#include <vector>

struct ColumnState
{
    std::vector<int> visible;
    std::vector<int> widths;
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
    bool showGridLines = false;
    int  saveFilterIndex = 1;
    bool excludeDuplicates = true;
    bool beepOnNewPassword = false;
    bool addExportHeaderLine = false;
    bool addToWindowsFirewall = true;
    int  captureMethod = 0;           // 0 Raw, 1 WinPcap, 2 NetMon, 3 NetMon3
    bool promiscuousMode = false;

    std::wstring pcapAdapterName;
    std::wstring adapterName;
    std::wstring netMonAdapterName;
    std::wstring netMon3AdapterName;

    ColumnState cols;

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path;

    Config() { cols.reset(kColumns, kColumnCount); }

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
        auto gi = [&](const wchar_t* n, int def) { return static_cast<int>(::GetPrivateProfileIntW(L"General", n, def, f)); };
        auto gs = [&](const wchar_t* n) {
            wchar_t buf[512]{};
            ::GetPrivateProfileStringW(L"General", n, L"", buf, 512, f);
            return std::wstring(buf);
        };

        showGridLines       = gi(L"ShowGridLines", 0) != 0;
        saveFilterIndex     = gi(L"SaveFilterIndex", 0);
        excludeDuplicates   = gi(L"ExcludeDuplicates", 1) != 0;
        beepOnNewPassword   = gi(L"BeepOnNewPassword", 0) != 0;
        addExportHeaderLine = gi(L"AddExportHeaderLine", 0) != 0;
        addToWindowsFirewall= gi(L"AddToWindowsFirewall", 1) != 0;
        captureMethod       = gi(L"CaptureMethod", 0);
        promiscuousMode     = gi(L"PromiscuousMode", 0) != 0;

        pcapAdapterName     = gs(L"PCapAdapterName");
        adapterName         = gs(L"AdapterName");
        netMonAdapterName   = gs(L"NetMonAdapterName");
        netMon3AdapterName  = gs(L"NetMon3AdapterName");

        loadColumns();

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
        auto ss = [&](const wchar_t* n, const std::wstring& v) {
            ::WritePrivateProfileStringW(L"General", n, v.c_str(), f);
        };
        si(L"ShowGridLines", showGridLines);
        si(L"SaveFilterIndex", saveFilterIndex);
        si(L"ExcludeDuplicates", excludeDuplicates);
        si(L"BeepOnNewPassword", beepOnNewPassword);
        si(L"AddExportHeaderLine", addExportHeaderLine);
        si(L"AddToWindowsFirewall", addToWindowsFirewall);
        si(L"CaptureMethod", captureMethod);
        ss(L"PCapAdapterName", pcapAdapterName);
        ss(L"AdapterName", adapterName);
        ss(L"NetMonAdapterName", netMonAdapterName);
        ss(L"NetMon3AdapterName", netMon3AdapterName);

        saveColumns();
        if (havePlacement) writeBlob(L"WinPos", &placement, sizeof(placement));
    }

private:
    // The shared config layer stores a binary value as uppercase hex byte
    // pairs joined by a single space and no trailing space, for example "2C 00 00 00".
    // toHex reproduces that exactly so the original can read this .cfg, and fromHex
    // skips the separators so it reads the original's. The shipped SniffPass.cfg uses
    // this encoding for WinPos and Columns.
    [[nodiscard]] static std::wstring toHex(const void* data, size_t bytes)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        s.reserve(bytes * 3);
        static const wchar_t* hx = L"0123456789ABCDEF";
        for (size_t i = 0; i < bytes; ++i)
        {
            if (i != 0) s += L' ';
            s += hx[p[i] >> 4]; s += hx[p[i] & 0xF];
        }
        return s;
    }
    [[nodiscard]] static std::vector<BYTE> fromHex(const std::wstring& s)
    {
        std::vector<BYTE> out;
        auto nib = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            return -1;
        };
        int hi = -1;
        for (wchar_t c : s)
        {
            int v = nib(c);
            if (v < 0) { hi = -1; continue; }
            if (hi < 0) { hi = v; }
            else { out.push_back(static_cast<BYTE>((hi << 4) | v)); hi = -1; }
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

    void loadColumns()
    {
        int s = static_cast<int>(::GetPrivateProfileIntW(L"General", L"Sort", 0, path.c_str()));
        cols.sortAscending = s >= 0;
        cols.sortColumn = (std::abs(s) < kColumnCount) ? std::abs(s) : 0;

        std::vector<BYTE> blob;
        if (!readBlob(L"Columns", blob) || blob.size() < 4) return;
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 3 < blob.size(); i += 4)
        {
            WORD w = blob[i] | (blob[i + 1] << 8);
            WORD c = blob[i + 2] | (blob[i + 3] << 8);
            if (c < kColumnCount) { vis.push_back(c); wid.push_back(w); }
        }
        if (!vis.empty()) { cols.visible = vis; cols.widths = wid; }
    }

    void saveColumns() const
    {
        wchar_t sb[16];
        swprintf(sb, 16, L"%d", cols.sortAscending ? cols.sortColumn : -cols.sortColumn);
        ::WritePrivateProfileStringW(L"General", L"Sort", sb, path.c_str());

        std::vector<BYTE> blob;
        for (size_t i = 0; i < cols.visible.size(); ++i)
        {
            WORD w = static_cast<WORD>(i < cols.widths.size() ? cols.widths[i] : 100);
            WORD c = static_cast<WORD>(cols.visible[i]);
            blob.push_back(w & 0xFF); blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF); blob.push_back((c >> 8) & 0xFF);
        }
        if (!blob.empty()) writeBlob(L"Columns", blob.data(), blob.size());
    }
};
