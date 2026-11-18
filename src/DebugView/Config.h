// Config.h -- the recreation's live options and their persistence.
//
// The original keeps a single opaque 400-byte "Settings" blob under
// HKCU\Software\Sysinternals\DbgView. This recreation keeps the same key but
// writes readable named values instead, which is a deliberate change noted in
// analysis/DebugView.md. The Sysinternals EULA flag is handled separately in
// Eula.h under HKCU\Software\Sysinternals\DebugView.
#pragma once
#include <windows.h>
#include <string>
#include "RegKey.h"
#include "Filter.h"
#include "Columns.h"

struct Config
{
    // capture sources
    bool captureEvents = true;    // master capture toggle
    bool captureWin32  = true;    // user-mode DBWIN source
    bool captureKernel = false;   // kernel source, driver out of scope

    // view options
    bool clockTime       = false; // false shows elapsed seconds, true wall clock
    bool showMs          = false; // milliseconds in clock-time mode
    bool win32Pids       = true;  // show the PID column
    bool autoScroll      = true;  // keep the newest line in view
    bool hideToolbar     = false;
    bool alwaysOnTop     = false;
    bool forceCR         = true;  // one list line per captured record
    int  historyDepth    = 0;     // 0 means keep everything

    Filter filter;

    int  colWidth[kColumnCount] = { 52, 95, 56, 760 };

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    static constexpr const wchar_t* kKey = L"Software\\Sysinternals\\DbgView";

    void load()
    {
        RegKey k;
        if (!k.open(HKEY_CURRENT_USER, kKey, KEY_READ))
            return;
        auto gb = [&](const wchar_t* n, bool def) { auto v = k.queryDword(n); return v ? (*v != 0) : def; };
        auto gi = [&](const wchar_t* n, int def)  { auto v = k.queryDword(n); return v ? static_cast<int>(*v) : def; };
        auto gs = [&](const wchar_t* n, const std::wstring& def) { auto v = k.queryString(n); return v ? *v : def; };

        captureEvents = gb(L"CaptureEvents", captureEvents);
        captureWin32  = gb(L"CaptureWin32", captureWin32);
        clockTime     = gb(L"ClockTime", clockTime);
        showMs        = gb(L"ShowMilliseconds", showMs);
        win32Pids     = gb(L"Win32Pids", win32Pids);
        autoScroll    = gb(L"AutoScroll", autoScroll);
        hideToolbar   = gb(L"HideToolbar", hideToolbar);
        alwaysOnTop   = gb(L"AlwaysOnTop", alwaysOnTop);
        forceCR       = gb(L"ForceCR", forceCR);
        historyDepth  = gi(L"HistoryDepth", historyDepth);

        filter.include   = gs(L"Include", filter.include);
        filter.exclude   = gs(L"Exclude", filter.exclude);
        filter.highlight = gs(L"Highlight", filter.highlight);

        for (int i = 0; i < kColumnCount; ++i)
        {
            wchar_t name[16];
            swprintf(name, 16, L"Col%d", i);
            colWidth[i] = gi(name, colWidth[i]);
        }

        if (auto blob = k.queryBinary(L"WinPos"); blob && blob->size() >= sizeof(WINDOWPLACEMENT))
        {
            memcpy(&placement, blob->data(), sizeof(placement));
            placement.length = sizeof(placement);
            havePlacement = true;
        }
    }

    void save() const
    {
        RegKey k;
        if (!k.create(HKEY_CURRENT_USER, kKey, KEY_READ | KEY_WRITE))
            return;
        k.setDword(L"CaptureEvents", captureEvents);
        k.setDword(L"CaptureWin32", captureWin32);
        k.setDword(L"ClockTime", clockTime);
        k.setDword(L"ShowMilliseconds", showMs);
        k.setDword(L"Win32Pids", win32Pids);
        k.setDword(L"AutoScroll", autoScroll);
        k.setDword(L"HideToolbar", hideToolbar);
        k.setDword(L"AlwaysOnTop", alwaysOnTop);
        k.setDword(L"ForceCR", forceCR);
        k.setDword(L"HistoryDepth", static_cast<DWORD>(historyDepth));
        k.setString(L"Include", filter.include);
        k.setString(L"Exclude", filter.exclude);
        k.setString(L"Highlight", filter.highlight);
        for (int i = 0; i < kColumnCount; ++i)
        {
            wchar_t name[16];
            swprintf(name, 16, L"Col%d", i);
            k.setDword(name, static_cast<DWORD>(colWidth[i]));
        }
        if (havePlacement)
            k.setBinary(L"WinPos", &placement, sizeof(placement));
    }
};
