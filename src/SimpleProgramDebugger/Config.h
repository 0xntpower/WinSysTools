// Config.h -- SimpleProgramDebugger settings. The original imports no registry API at all
// and keeps no registry key. It reads and writes a SimpleProgramDebugger.cfg INI file next
// to the executable, section [General], through WritePrivateProfileStringW. Value names
// recovered from the binary: MarkOddEvenRows, ShowGridLines, SaveFilterIndex, ShowInfoTip,
// ScrollDownOnNewLine, NewProcess.ProcessPath, NewProcess.Params, NewProcess.StartFolder,
// SortOnEveryUpdate, TrayIcon, StartAsHidden, AddExportHeaderLine, WinPos, Sort, Columns.
// The Columns blob is an array of (WORD width, WORD columnIndex) in display order, and the
// binary blobs WinPos and Columns are kept as hex strings the way the shared config
// layer encodes them.
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
    bool listViewSortProcess = false;  // carried for config fidelity, original default 0
    bool scrollDownOnNewLine = true;
    bool sortOnEveryUpdate = false;
    bool addExportHeaderLine = true;
    bool trayIcon = false;
    bool startAsHidden = false;

    int  saveFilterIndex = 1;

    std::wstring newProcessPath;
    std::wstring newProcessParams;
    std::wstring newProcessStartFolder;

    bool sorted = false;               // false -> chronological insertion order
    int  sortColumn = 0;
    bool sortAscending = true;

    std::vector<int> visibleColumns;   // display order (internal indices)
    std::vector<int> columnWidths;     // per visible column

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path;                 // the .cfg file, resolved once

    Config() { resetColumns(); }

    [[nodiscard]] static std::wstring DefaultPath()
    {
        wchar_t exe[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        size_t dot = p.rfind(L'.');
        if (dot != std::wstring::npos) p.resize(dot);
        return p + L".cfg";
    }

    void resetColumns()
    {
        visibleColumns.assign(std::begin(kDefaultOrder), std::end(kDefaultOrder));
        columnWidths.clear();
        for (int c : visibleColumns) columnWidths.push_back(kColumns[c].width);
    }

    void load()
    {
        if (path.empty()) path = DefaultPath();
        const wchar_t* f = path.c_str();
        auto gi = [&](const wchar_t* n, int def) { return static_cast<int>(::GetPrivateProfileIntW(L"General", n, def, f)); };
        auto gs = [&](const wchar_t* n) {
            wchar_t buf[1024]{};
            ::GetPrivateProfileStringW(L"General", n, L"", buf, 1024, f);
            return std::wstring(buf);
        };

        markOddEvenRows     = gi(L"MarkOddEvenRows", markOddEvenRows) != 0;
        showGridLines       = gi(L"ShowGridLines", showGridLines) != 0;
        showInfoTip         = gi(L"ShowInfoTip", showInfoTip) != 0;
        listViewSortProcess = gi(L"ListViewSortProcess", listViewSortProcess) != 0;
        scrollDownOnNewLine = gi(L"ScrollDownOnNewLine", scrollDownOnNewLine) != 0;
        sortOnEveryUpdate   = gi(L"SortOnEveryUpdate", sortOnEveryUpdate) != 0;
        addExportHeaderLine = gi(L"AddExportHeaderLine", addExportHeaderLine) != 0;
        trayIcon            = gi(L"TrayIcon", trayIcon) != 0;
        startAsHidden       = gi(L"StartAsHidden", startAsHidden) != 0;
        saveFilterIndex     = gi(L"SaveFilterIndex", saveFilterIndex);

        if (std::wstring s = gs(L"NewProcess.ProcessPath"); !s.empty()) newProcessPath = s;
        if (std::wstring s = gs(L"NewProcess.Params"); !s.empty()) newProcessParams = s;
        if (std::wstring s = gs(L"NewProcess.StartFolder"); !s.empty()) newProcessStartFolder = s;

        int s = gi(L"Sort", encodedSort());
        if (s == 0)
        {
            sorted = false;
        }
        else
        {
            sorted = true;
            sortAscending = s > 0;
            sortColumn = std::abs(s) - 1;
        }
        if (sortColumn < 0 || sortColumn >= kColumnCount) { sorted = false; sortColumn = 0; }

        loadColumns(gs(L"Columns"));
        loadPlacement(gs(L"WinPos"));
    }

    void save() const
    {
        const std::wstring p = path.empty() ? DefaultPath() : path;
        const wchar_t* f = p.c_str();
        auto si = [&](const wchar_t* n, int v) {
            wchar_t b[16]{};
            swprintf(b, 16, L"%d", v);
            ::WritePrivateProfileStringW(L"General", n, b, f);
        };
        auto ss = [&](const wchar_t* n, const std::wstring& v) {
            ::WritePrivateProfileStringW(L"General", n, v.c_str(), f);
        };

        si(L"MarkOddEvenRows", markOddEvenRows);
        si(L"ShowGridLines", showGridLines);
        si(L"ShowInfoTip", showInfoTip);
        si(L"ListViewSortProcess", listViewSortProcess);
        si(L"ScrollDownOnNewLine", scrollDownOnNewLine);
        si(L"SortOnEveryUpdate", sortOnEveryUpdate);
        si(L"AddExportHeaderLine", addExportHeaderLine);
        si(L"TrayIcon", trayIcon);
        si(L"StartAsHidden", startAsHidden);
        si(L"SaveFilterIndex", saveFilterIndex);
        ss(L"NewProcess.ProcessPath", newProcessPath);
        ss(L"NewProcess.Params", newProcessParams);
        ss(L"NewProcess.StartFolder", newProcessStartFolder);
        si(L"Sort", encodedSort());

        ss(L"Columns", saveColumns());
        if (havePlacement)
            ss(L"WinPos", toHex(&placement, sizeof(placement)));
    }

    void deleteConfig() const
    {
        ::DeleteFileW((path.empty() ? DefaultPath() : path).c_str());
    }

private:
    [[nodiscard]] int encodedSort() const
    {
        if (!sorted) return 0;
        return sortAscending ? (sortColumn + 1) : -(sortColumn + 1);
    }

    // The INI layer encodes a blob as space-separated uppercase hex bytes ("%02X ").
    [[nodiscard]] static std::wstring toHex(const void* data, size_t bytes)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        s.reserve(bytes * 3);
        static const wchar_t* hx = L"0123456789ABCDEF";
        for (size_t i = 0; i < bytes; ++i) { if (i) s += L' '; s += hx[p[i] >> 4]; s += hx[p[i] & 0xF]; }
        return s;
    }
    [[nodiscard]] static std::vector<BYTE> fromHex(const std::wstring& s)
    {
        auto nib = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            return -1;
        };
        std::vector<BYTE> out;
        int hi = -1;
        for (wchar_t c : s)                      // tolerates spaced or continuous hex
        {
            int v = nib(c);
            if (v < 0) continue;
            if (hi < 0) { hi = v; }
            else { out.push_back(static_cast<BYTE>((hi << 4) | v)); hi = -1; }
        }
        return out;
    }

    [[nodiscard]] std::wstring saveColumns() const
    {
        std::vector<BYTE> blob;
        for (size_t i = 0; i < visibleColumns.size(); ++i)
        {
            WORD w = static_cast<WORD>(i < columnWidths.size() ? columnWidths[i] : kColumns[visibleColumns[i]].width);
            WORD c = static_cast<WORD>(visibleColumns[i]);
            blob.push_back(w & 0xFF); blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF); blob.push_back((c >> 8) & 0xFF);
        }
        return toHex(blob.data(), blob.size());
    }
    void loadColumns(const std::wstring& hex)
    {
        std::vector<BYTE> blob = fromHex(hex);
        if (blob.size() < 4) return;
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 3 < blob.size(); i += 4)
        {
            WORD w = static_cast<WORD>(blob[i] | (blob[i + 1] << 8));
            WORD c = static_cast<WORD>(blob[i + 2] | (blob[i + 3] << 8));
            if (c < kColumnCount) { vis.push_back(c); wid.push_back(w); }
        }
        if (!vis.empty()) { visibleColumns = vis; columnWidths = wid; }
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
