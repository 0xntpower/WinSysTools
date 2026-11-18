// Addin.h -- one Office COM/automation add-in record, reconstructed from the record
// the enumerator fills (sub_140002C8C per add-in, sub_140002874 CLSID resolution,
// sub_1400026B8 file version info). The column() getter reproduces sub_140001BC4.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// The eight Office applications scanned, in enumeration order (appIndex 1..8). The
// registry subkey uses "MS Project" while the displayed Software column shows "Project".
inline const wchar_t* const kScanApps[] = {
    L"Word", L"Excel", L"Outlook", L"PowerPoint", L"MS Project", L"Access", L"Visio", L"FrontPage"
};
inline const wchar_t* const kAppNames[] = {
    L"Word", L"Excel", L"Outlook", L"PowerPoint", L"Project", L"Access", L"Visio", L"FrontPage"
};

struct Addin
{
    std::wstring progId;          // Addins subkey name          (col 0)
    std::wstring description;     // Description / FriendlyName   (col 1)
    int          appIndex = 1;    // 1..8, Office application     (col 2)
    DWORD        loadBehavior = 0;// LoadBehavior value           (cols 3, 4)
    std::wstring productName;     // version ProductName          (col 5)
    std::wstring fileVersion;     // version FileVersion          (col 6)
    std::wstring companyName;     // version CompanyName           (col 7)
    std::wstring fileDescription; // version FileDescription      (col 8)
    FILETIME     createdTime{};   // file creation time (local)   (col 9)
    bool         hasFile = false; // whether the server file exists, for created time
    std::wstring fileName;        // InprocServer32 / LocalServer32 (col 10)
    std::wstring clsid;           // add-in CLSID                 (col 11)
    bool         isDotNet = false;// mscoree.dll host             (col 12)
    std::wstring dotNetVersion;   // managed RuntimeVersion       (col 13)
    HKEY         rootKey = HKEY_CURRENT_USER;   // which hive     (col 14)
    std::wstring regSubPath;      // "Software\...\Addins\<progid>" (col 14)

    // Start Mode (col 3): classify LoadBehavior exactly as sub_140001BC4 does --
    // test bit 1, then bit 3, then bit 4, then the <=1 case, in that order.
    std::wstring startMode() const
    {
        if (loadBehavior & 2)    return L"Load at Startup";
        if (loadBehavior & 8)    return L"Load on Demand";
        if (loadBehavior & 0x10) return L"Load at First Time";
        if (loadBehavior <= 1)   return L"Disabled";
        return {};
    }

    std::wstring connected() const { return (loadBehavior & 1) ? L"Yes" : L"No"; }

    std::wstring registryKey() const
    {
        const wchar_t* root = (rootKey == HKEY_LOCAL_MACHINE) ? L"HKEY_LOCAL_MACHINE" : L"HKEY_CURRENT_USER";
        return std::wstring(root) + L"\\" + regSubPath;
    }

    std::wstring createdTimeText() const;  // formatted local date+time, "N/A" if none

    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return progId;
        case 1:  return description;
        case 2:  return kAppNames[(appIndex >= 1 && appIndex <= 8) ? appIndex - 1 : 0];
        case 3:  return startMode();
        case 4:  return connected();
        case 5:  return productName;
        case 6:  return fileVersion;
        case 7:  return companyName;
        case 8:  return fileDescription;
        case 9:  return createdTimeText();
        case 10: return fileName;
        case 11: return clsid;
        case 12: return isDotNet ? L"Yes" : L"No";
        case 13: return dotNetVersion;
        case 14: return registryKey();
        default: return {};
        }
    }
};

// Scan every Office add-in registry location (HKCU + HKLM, 8 apps, 6 root layouts).
// remoteComputer empty = local machine.
[[nodiscard]] std::vector<Addin> EnumerateAddins(const std::wstring& remoteComputer = L"");

// Apply a LoadBehavior change (OR value, AND mask) to one add-in, writing it back to
// the registry, and update the in-memory record. Mirrors sub_140002580.
bool SetLoadBehavior(Addin& a, DWORD orValue, DWORD andMask, const std::wstring& remoteComputer = L"");

// CLI helpers (mirror sub_14000A644 / sub_140002580 mask tables).
bool SetStartMode(Addin& a, int mode, const std::wstring& remoteComputer = L"");  // 0..3
bool SetConnected(Addin& a, int yes, const std::wstring& remoteComputer = L"");   // 0..1

// Does this add-in match a CLI name (ProgID / CLSID / full path / dll basename)?
[[nodiscard]] bool AddinMatchesName(const Addin& a, const std::wstring& name);
