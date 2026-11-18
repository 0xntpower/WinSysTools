// Config.h -- SmartSniff settings. The original keeps a SmartSniff.cfg INI file next to
// the executable, section [General], read and written through the shared INI
// layer. Value names and defaults match the binary (recovered from the settings reader).
// The binary blobs WinPos and Columns are stored as uppercase hex strings, the way that
// layer encodes them, so this file interoperates with the original's .cfg.
#pragma once
#include "Columns.h"
#include <windows.h>
#include <string>
#include <vector>

struct ColumnState
{
    std::vector<int> visible; // display order, internal indices
    std::vector<int> widths;  // per visible column
    int sortColumn = 0;
    bool sortAscending = true;

    void reset(const ColumnDef* defs, int count)
    {
        visible.clear();
        widths.clear();
        for (int i = 0; i < count; ++i)
        {
            visible.push_back(i);
            widths.push_back(defs[i].width);
        }
        sortColumn = 0;
        sortAscending = true;
    }
};

struct Config
{
    // View menu
    bool showGridLines = false;   // ShowGridLines 0
    bool showInfoTip = true;      // ShowInfoTip 1
    bool markOddEvenRows = false; // MarkOddEvenRows 0

    // Options menu toggles
    bool resolveIP = true;             // ResolveIPAddr 1
    bool useDns = true;                // UseDnsQueriesForHostNames 1
    bool displayCaptureTime = false;   // DisplayCaptureTime 0
    bool showOutgoingIncoming = false; // ShowOutgoingIncoming 0
    bool alwaysOnTop = false;          // AlwaysOnTop 0
    bool hideLowerPane = false;        // HideLowerPane 0
    bool displayHighChars = false;     // DisplayHighChars 0

    // Display Mode and Display Protocols
    int displayMode = 1;     // DisplayMode 1 (1 Automatic, 2 Ascii, 3 Hex, 4 URL)
    bool displayTcp = true;  // DisplayTCP 1
    bool displayUdp = true;  // DisplayUDP 1
    bool displayIcmp = true; // DisplayICMP 1

    // Export
    int saveFilterIndex = 1;          // SaveFilterIndex (clamped to 1 for the dialog)
    bool addExportHeaderLine = false; // AddExportHeaderLine 0

    // Capture
    int captureMethod = 1;              // CaptureMethod 1 (1 Raw, 2 WinPcap, 3 NetMon, 4 NetMon3)
    bool promiscuousMode = false;       // PromiscuousMode 0
    bool addToWindowsFirewall = true;   // AddToWindowsFirewall 1
    bool captureOnProgramStart = false; // CaptureOnProgramStart 0
    std::wstring adapterName;           // AdapterName
    std::wstring pcapAdapterName;       // PCapAdapterName
    std::wstring captureFilter;         // CaptureFilter
    std::wstring displayFilter;         // DisplayFilter

    // Layout
    int splitLoc = 0x3FFF; // SplitLoc 0x3FFF
    ColumnState cols;

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring path; // the .cfg file, resolved once

    Config()
    {
        cols.reset(kColumns, kColumnCount);
    }

    static std::wstring DefaultPath()
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

        showGridLines = gi(L"ShowGridLines", 0) != 0;
        showInfoTip = gi(L"ShowInfoTip", 1) != 0;
        markOddEvenRows = gi(L"MarkOddEvenRows", 0) != 0;
        resolveIP = gi(L"ResolveIPAddr", 1) != 0;
        useDns = gi(L"UseDnsQueriesForHostNames", 1) != 0;
        displayCaptureTime = gi(L"DisplayCaptureTime", 0) != 0;
        showOutgoingIncoming = gi(L"ShowOutgoingIncoming", 0) != 0;
        alwaysOnTop = gi(L"AlwaysOnTop", 0) != 0;
        hideLowerPane = gi(L"HideLowerPane", 0) != 0;
        displayHighChars = gi(L"DisplayHighChars", 0) != 0;
        displayMode = gi(L"DisplayMode", 1);
        displayTcp = gi(L"DisplayTCP", 1) != 0;
        displayUdp = gi(L"DisplayUDP", 1) != 0;
        displayIcmp = gi(L"DisplayICMP", 1) != 0;
        saveFilterIndex = gi(L"SaveFilterIndex", 0);
        if (saveFilterIndex < 1)
        {
            saveFilterIndex = 1;
        }
        addExportHeaderLine = gi(L"AddExportHeaderLine", 0) != 0;
        captureMethod = gi(L"CaptureMethod", 1);
        promiscuousMode = gi(L"PromiscuousMode", 0) != 0;
        addToWindowsFirewall = gi(L"AddToWindowsFirewall", 1) != 0;
        captureOnProgramStart = gi(L"CaptureOnProgramStart", 0) != 0;
        splitLoc = gi(L"SplitLoc", 0x3FFF);

        adapterName = readStr(L"AdapterName");
        pcapAdapterName = readStr(L"PCapAdapterName");
        captureFilter = readStr(L"CaptureFilter");
        displayFilter = readStr(L"DisplayFilter");

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
        if (path.empty())
        {
            return;
        }
        const wchar_t* f = path.c_str();
        auto si = [&](const wchar_t* n, int v)
        {
            wchar_t b[16];
            swprintf(b, 16, L"%d", v);
            ::WritePrivateProfileStringW(L"General", n, b, f);
        };
        si(L"ShowGridLines", showGridLines);
        si(L"ShowInfoTip", showInfoTip);
        si(L"MarkOddEvenRows", markOddEvenRows);
        si(L"ResolveIPAddr", resolveIP);
        si(L"UseDnsQueriesForHostNames", useDns);
        si(L"DisplayCaptureTime", displayCaptureTime);
        si(L"ShowOutgoingIncoming", showOutgoingIncoming);
        si(L"AlwaysOnTop", alwaysOnTop);
        si(L"HideLowerPane", hideLowerPane);
        si(L"DisplayHighChars", displayHighChars);
        si(L"DisplayMode", displayMode);
        si(L"DisplayTCP", displayTcp);
        si(L"DisplayUDP", displayUdp);
        si(L"DisplayICMP", displayIcmp);
        si(L"SaveFilterIndex", saveFilterIndex);
        si(L"AddExportHeaderLine", addExportHeaderLine);
        si(L"CaptureMethod", captureMethod);
        si(L"PromiscuousMode", promiscuousMode);
        si(L"AddToWindowsFirewall", addToWindowsFirewall);
        si(L"CaptureOnProgramStart", captureOnProgramStart);
        si(L"SplitLoc", splitLoc);

        ::WritePrivateProfileStringW(L"General", L"AdapterName", adapterName.c_str(), f);
        ::WritePrivateProfileStringW(L"General", L"PCapAdapterName", pcapAdapterName.c_str(), f);
        ::WritePrivateProfileStringW(L"General", L"CaptureFilter", captureFilter.c_str(), f);
        ::WritePrivateProfileStringW(L"General", L"DisplayFilter", displayFilter.c_str(), f);

        saveColumns();
        if (havePlacement)
        {
            writeBlob(L"WinPos", &placement, sizeof(placement));
        }
    }

  private:
    std::wstring readStr(const wchar_t* name) const
    {
        wchar_t buf[1024]{};
        ::GetPrivateProfileStringW(L"General", name, L"", buf, 1024, path.c_str());
        return buf;
    }

    // The shared config layer stores a binary value as uppercase hex byte
    // pairs joined by a single space and no trailing space, for example "2C 00 00 00".
    // toHex reproduces that exactly so the original can read this .cfg, and fromHex
    // skips the separators so it reads the original's.
    [[nodiscard]] static std::wstring toHex(const void* data, size_t bytes)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        s.reserve(bytes * 3);
        static const wchar_t* hx = L"0123456789ABCDEF";
        for (size_t i = 0; i < bytes; ++i)
        {
            if (i != 0)
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
        std::vector<BYTE> out;
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
        int hi = -1;
        for (wchar_t c : s)
        {
            int v = nib(c);
            if (v < 0)
            {
                hi = -1;
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

    bool readBlob(const wchar_t* name, std::vector<BYTE>& out) const
    {
        wchar_t buf[4096]{};
        ::GetPrivateProfileStringW(L"General", name, L"", buf, 4096, path.c_str());
        if (buf[0] == 0)
        {
            return false;
        }
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
        if (!readBlob(L"Columns", blob) || blob.size() < 4)
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
            cols.visible = vis;
            cols.widths = wid;
        }
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
            blob.push_back(w & 0xFF);
            blob.push_back((w >> 8) & 0xFF);
            blob.push_back(c & 0xFF);
            blob.push_back((c >> 8) & 0xFF);
        }
        if (!blob.empty())
        {
            writeBlob(L"Columns", blob.data(), blob.size());
        }
    }
};
