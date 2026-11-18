// Config.h -- OutlookAttachView settings. The original keeps its settings in an
// INI .cfg file next to the executable (OutlookAttachView.cfg by default, or the
// file named by /cfg), read with GetPrivateProfileInt and WritePrivateProfile.
// The value names here match the original binary (recovered from sub_140002524).
// Window placement is stored as a hex blob under WinPos, the same idea as the
// original's binary WinPos value.
#pragma once
#include "Columns.h"
#include <windows.h>
#include <vector>
#include <string>

// The original's config object hardcodes the INI section "General" (recovered from
// the config constructor sub_140022558, which copies L"General" regardless of the
// tool name), so every value lives under [General], not under the tool name.
inline const wchar_t* kConfigSection = L"General";

struct Config
{
    bool showGridLines = false;
    bool showInfoTip = true;
    bool showInlineAttachment = true;
    bool showTimeInGmt = false;
    bool markOddEvenRows = false;
    bool markInlineAttachments = false;
    bool blackBackground = false;
    bool alignNumbersToRight = false;
    bool addExportHeaderLine = false;
    bool alwaysOnTop = false;
    bool trayIcon = false;
    bool previewPane = false;
    bool showOptionsBeforeScan = true;

    int  fileSizeUnit = 1;             // 1 Bytes, 2 KB, 3 MB, 4 GB, 5 Automatic
    int  saveFileEncoding = 0;         // 0 Default, 1 ANSI, 2 UTF-16, 3 UTF-8
    int  saveFilterIndex = 1;
    int  doubleClickAction = 1;
    int  enterKeyAction = 0;

    int  sortColumn = 0;               // internal column index
    bool sortAscending = true;

    std::vector<int> visibleColumns;   // display order (internal indices)
    std::vector<int> columnWidths;     // per visible column

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path;                 // the .cfg file in use

    Config() { resetColumns(); path = defaultPath(); }

    static std::wstring defaultPath()
    {
        wchar_t buf[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring p = buf;
        size_t dot = p.find_last_of(L'.');
        if (dot != std::wstring::npos) p.erase(dot);
        return p + L".cfg";
    }

    void resetColumns()
    {
        visibleColumns.assign(std::begin(kDefaultOrder), std::end(kDefaultOrder));
        columnWidths.clear();
        for (int c : visibleColumns) columnWidths.push_back(kColumns[c].width);
    }

    int gi(const wchar_t* name, int def) const
    {
        return static_cast<int>(::GetPrivateProfileIntW(kConfigSection, name, def, path.c_str()));
    }
    std::wstring gs(const wchar_t* name) const
    {
        wchar_t buf[4096]{};
        ::GetPrivateProfileStringW(kConfigSection, name, L"", buf, 4096, path.c_str());
        return buf;
    }
    void si(const wchar_t* name, int v) const
    {
        wchar_t b[32]; wsprintfW(b, L"%d", v);
        ::WritePrivateProfileStringW(kConfigSection, name, b, path.c_str());
    }

    void load()
    {
        showGridLines = gi(L"ShowGridLines", showGridLines) != 0;
        showInfoTip = gi(L"ShowInfoTip", showInfoTip) != 0;
        showInlineAttachment = gi(L"ShowInlineAttachment", showInlineAttachment) != 0;
        showTimeInGmt = gi(L"ShowTimeInGMT", showTimeInGmt) != 0;
        markOddEvenRows = gi(L"MarkOddEvenRows", markOddEvenRows) != 0;
        markInlineAttachments = gi(L"MarkInlineAttachments", markInlineAttachments) != 0;
        blackBackground = gi(L"BlackBackground", blackBackground) != 0;
        alignNumbersToRight = gi(L"AlignNumbersToRight", alignNumbersToRight) != 0;
        addExportHeaderLine = gi(L"AddExportHeaderLine", addExportHeaderLine) != 0;
        alwaysOnTop = gi(L"AlwaysOnTop", alwaysOnTop) != 0;
        trayIcon = gi(L"TrayIcon", trayIcon) != 0;
        previewPane = gi(L"PreviewPane", previewPane) != 0;
        showOptionsBeforeScan = gi(L"ShowOptionsBeforeScan", showOptionsBeforeScan) != 0;
        fileSizeUnit = gi(L"FileSizeUnit", fileSizeUnit);
        saveFileEncoding = gi(L"SaveFileEncoding", saveFileEncoding);
        saveFilterIndex = gi(L"SaveFilterIndex", saveFilterIndex);
        doubleClickAction = gi(L"DoubleClickAction", doubleClickAction);
        enterKeyAction = gi(L"EnterKeyAction", enterKeyAction);
        int s = gi(L"Sort", 0);
        sortAscending = s >= 0;
        sortColumn = (std::abs(s) < kColumnCount) ? std::abs(s) : 0;
        loadColumns();
        loadPlacement();
    }

    void save() const
    {
        si(L"ShowGridLines", showGridLines);
        si(L"ShowInfoTip", showInfoTip);
        si(L"ShowInlineAttachment", showInlineAttachment);
        si(L"ShowTimeInGMT", showTimeInGmt);
        si(L"MarkOddEvenRows", markOddEvenRows);
        si(L"MarkInlineAttachments", markInlineAttachments);
        si(L"BlackBackground", blackBackground);
        si(L"AlignNumbersToRight", alignNumbersToRight);
        si(L"AddExportHeaderLine", addExportHeaderLine);
        si(L"AlwaysOnTop", alwaysOnTop);
        si(L"TrayIcon", trayIcon);
        si(L"PreviewPane", previewPane);
        si(L"ShowOptionsBeforeScan", showOptionsBeforeScan);
        si(L"FileSizeUnit", fileSizeUnit);
        si(L"SaveFileEncoding", saveFileEncoding);
        si(L"SaveFilterIndex", saveFilterIndex);
        si(L"DoubleClickAction", doubleClickAction);
        si(L"EnterKeyAction", enterKeyAction);
        si(L"Sort", sortAscending ? sortColumn : -sortColumn);
        saveColumns();
        savePlacement();
    }

private:
    // The shared NirSoft config layer stores a binary value as uppercase hex byte
    // pairs joined by a single space and no trailing space, for example "2C 00 00 00"
    // (recovered from the serializer sub_14000B064, which formats each byte with
    // "%2.2X " and overwrites the final space with the terminator). toHex reproduces
    // that so the original can read this .cfg, and fromHex skips the separators so it
    // reads the original's.
    [[nodiscard]] static std::wstring toHex(const void* data, size_t bytes)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s; s.reserve(bytes * 3);
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

    // Columns: one 4-byte record per displayed column, width (LE WORD) then internal
    // index (LE WORD), serialized as the hex blob above. Matches the original's
    // sub_14000E368 / sub_14000DEA0 column serialization.
    void loadColumns()
    {
        std::vector<BYTE> blob = fromHex(gs(L"Columns"));
        if (blob.size() < 4) return;
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 3 < blob.size(); i += 4)
        {
            WORD w = static_cast<WORD>(blob[i] | (blob[i + 1] << 8));
            WORD c = static_cast<WORD>(blob[i + 2] | (blob[i + 3] << 8));
            if (c < kColumnCount) { vis.push_back(c); wid.push_back(w > 0 ? w : kColumns[c].width); }
        }
        if (!vis.empty()) { visibleColumns = vis; columnWidths = wid; }
    }

    void saveColumns() const
    {
        std::vector<BYTE> blob;
        for (size_t i = 0; i < visibleColumns.size(); ++i)
        {
            WORD w = static_cast<WORD>(i < columnWidths.size() ? columnWidths[i] : kColumns[visibleColumns[i]].width);
            WORD c = static_cast<WORD>(visibleColumns[i]);
            blob.push_back(w & 0xFF); blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF); blob.push_back((c >> 8) & 0xFF);
        }
        if (!blob.empty())
        {
            std::wstring hex = toHex(blob.data(), blob.size());
            ::WritePrivateProfileStringW(kConfigSection, L"Columns", hex.c_str(), path.c_str());
        }
    }

    void loadPlacement()
    {
        std::vector<BYTE> blob = fromHex(gs(L"WinPos"));
        if (blob.size() < sizeof(WINDOWPLACEMENT)) return;
        memcpy(&placement, blob.data(), sizeof(WINDOWPLACEMENT));
        placement.length = sizeof(placement);
        havePlacement = true;
    }

    void savePlacement() const
    {
        if (!havePlacement) return;
        std::wstring hex = toHex(&placement, sizeof(WINDOWPLACEMENT));
        ::WritePrivateProfileStringW(kConfigSection, L"WinPos", hex.c_str(), path.c_str());
    }
};
