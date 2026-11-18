// Config.h -- SimpleWMIView settings. The original persists its configuration in a
// .cfg INI file next to the executable (<exe>.cfg), section [General], not in the
// registry: the binary imports only the GetPrivateProfile*/WritePrivateProfile*
// APIs and no registry value writer (path builder sub_140011B78, value table
// sub_1400020F8). The /cfg switch overrides the file path. Value names and defaults
// match the original.
#pragma once
#include "../common/CfgIni.h"
#include <windows.h>
#include <string>
#include <cstdlib>

// Save-file encoding (SaveFileEncoding value).
enum SaveEncoding { Enc_Default = 0, Enc_Ansi = 1, Enc_Unicode = 2, Enc_Utf8 = 3 };

struct Config
{
    // WMI query (defaults recovered: root\CIMV2 / Win32_Process)
    std::wstring nameSpace = L"root\\CIMV2";
    std::wstring classOrQuery = L"Win32_Process";
    int  connectComputer = 1;       // 1 this computer, 2 remote
    std::wstring computerName;
    std::wstring userName;
    int  syncMode = 1;              // 1 asynchronous, 2 synchronous
    int  timeout  = 15000;          // ms

    // display / options
    bool showGridLines = false;
    bool showInfoTip = true;
    bool markOddEvenRows = false;
    bool autoColumnsSize = false;
    bool autoFillClassesCombo = true;
    bool classesAutoComplete = false;
    bool alignNumbersToRight = false;
    bool showTimeInGmt = false;
    bool showMilliseconds = false;
    bool blackBackground = false;
    bool autoUpdateOnClassChange = false;
    bool enumerateFullNamespaceTree = true;
    bool trayIcon = false;
    bool startAsHidden = false;
    bool alwaysOnTop = false;

    int  saveFileEncoding = Enc_Default;
    int  saveFilterIndex = 0;

    int  sortColumn = 0;            // sort column index
    bool sortAscending = true;

    bool havePlacement = false;
    WINDOWPLACEMENT placement{};

    std::wstring cfgPath = cfgini::DefaultPath();

    // /cfg command-line override: point the store at a different file.
    void setCfgPath(const std::wstring& arg) { cfgPath = cfgini::Resolve(arg); }

    void load()
    {
        const std::wstring& p = cfgPath;
        auto rb = [&](const wchar_t* n, bool& v) { v = cfgini::GetInt(p, n, v) != 0; };
        auto ri = [&](const wchar_t* n, int& v)  { v = cfgini::GetInt(p, n, v); };

        nameSpace = cfgini::GetStr(p, L"Namespace", nameSpace.c_str());
        classOrQuery = cfgini::GetStr(p, L"Class", classOrQuery.c_str());
        ri(L"ConnectComputer", connectComputer);
        computerName = cfgini::GetStr(p, L"ComputerName");
        userName = cfgini::GetStr(p, L"UserName");
        ri(L"SyncMode", syncMode);
        ri(L"Timeout", timeout);

        rb(L"ShowGridLines", showGridLines);
        rb(L"ShowInfoTip", showInfoTip);
        rb(L"MarkOddEvenRows", markOddEvenRows);
        rb(L"AutoColumnsSize", autoColumnsSize);
        rb(L"AutoFillClassesCombo", autoFillClassesCombo);
        rb(L"ClassesAutoComplete", classesAutoComplete);
        rb(L"AlignNumbersToRight", alignNumbersToRight);
        rb(L"ShowTimeInGMT", showTimeInGmt);
        rb(L"ShowMilliseconds", showMilliseconds);
        rb(L"BlackBackground", blackBackground);
        rb(L"AutoUpdateOnClassChange", autoUpdateOnClassChange);
        rb(L"EnumerateFullNamespaceTree", enumerateFullNamespaceTree);
        rb(L"TrayIcon", trayIcon);
        rb(L"StartAsHidden", startAsHidden);
        rb(L"AlwaysOnTop", alwaysOnTop);
        ri(L"SaveFileEncoding", saveFileEncoding);
        ri(L"SaveFilterIndex", saveFilterIndex);

        const int s = cfgini::GetInt(p, L"Sort", 0);
        sortAscending = s >= 0;
        sortColumn = std::abs(s);

        auto wp = cfgini::GetBlob(p, L"WinPos");
        if (wp.size() >= sizeof(WINDOWPLACEMENT))
        {
            memcpy(&placement, wp.data(), sizeof(placement));
            placement.length = sizeof(placement);
            havePlacement = true;
        }
    }

    void save() const
    {
        const std::wstring& p = cfgPath;
        cfgini::PutStr(p, L"Namespace", nameSpace);
        cfgini::PutStr(p, L"Class", classOrQuery);
        cfgini::PutInt(p, L"ConnectComputer", connectComputer);
        cfgini::PutStr(p, L"ComputerName", computerName);
        cfgini::PutStr(p, L"UserName", userName);
        cfgini::PutInt(p, L"SyncMode", syncMode);
        cfgini::PutInt(p, L"Timeout", timeout);

        cfgini::PutInt(p, L"ShowGridLines", showGridLines);
        cfgini::PutInt(p, L"ShowInfoTip", showInfoTip);
        cfgini::PutInt(p, L"MarkOddEvenRows", markOddEvenRows);
        cfgini::PutInt(p, L"AutoColumnsSize", autoColumnsSize);
        cfgini::PutInt(p, L"AutoFillClassesCombo", autoFillClassesCombo);
        cfgini::PutInt(p, L"ClassesAutoComplete", classesAutoComplete);
        cfgini::PutInt(p, L"AlignNumbersToRight", alignNumbersToRight);
        cfgini::PutInt(p, L"ShowTimeInGMT", showTimeInGmt);
        cfgini::PutInt(p, L"ShowMilliseconds", showMilliseconds);
        cfgini::PutInt(p, L"BlackBackground", blackBackground);
        cfgini::PutInt(p, L"AutoUpdateOnClassChange", autoUpdateOnClassChange);
        cfgini::PutInt(p, L"EnumerateFullNamespaceTree", enumerateFullNamespaceTree);
        cfgini::PutInt(p, L"TrayIcon", trayIcon);
        cfgini::PutInt(p, L"StartAsHidden", startAsHidden);
        cfgini::PutInt(p, L"AlwaysOnTop", alwaysOnTop);
        cfgini::PutInt(p, L"SaveFileEncoding", saveFileEncoding);
        cfgini::PutInt(p, L"SaveFilterIndex", saveFilterIndex);
        cfgini::PutInt(p, L"Sort", sortAscending ? sortColumn : -sortColumn);
        if (havePlacement)
        {
            cfgini::PutBlob(p, L"WinPos", &placement, sizeof(placement));
        }
    }
};
