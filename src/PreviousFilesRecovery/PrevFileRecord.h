// PrevFileRecord.h -- one scan result row. The field-to-column mapping matches the
// original's column-text builder (sub_1400024CC). The "Current *" columns are blank
// for Deleted File and Deleted Folder rows, exactly as the original leaves them.
#pragma once
#include <windows.h>
#include <string>
#include "Columns.h"
#include "Format.h"

enum class ItemType { OlderFileVersion = 1, DeletedFile = 2, DeletedFolder = 3 };

inline const wchar_t* ItemTypeText(ItemType t)
{
    switch (t)
    {
    case ItemType::OlderFileVersion: return L"Older File Version";
    case ItemType::DeletedFile:      return L"Deleted File";
    case ItemType::DeletedFolder:    return L"Deleted Folder";
    }
    return L"";
}

struct PrevFileRecord
{
    std::wstring filename;         // col 0
    std::wstring folder;           // col 1  (full folder path on the live drive)
    ItemType     type = ItemType::OlderFileVersion;  // col 2

    FILETIME     currentModified{};   // col 3  (Older File Version only)
    FILETIME     previousModified{};  // col 4
    unsigned long long currentSize = 0;   // col 5  (Older File Version only)
    unsigned long long previousSize = 0;  // col 6
    DWORD        currentAttrs = 0;   // col 7  (Older File Version only)
    DWORD        previousAttrs = 0;  // col 8

    std::wstring shadowName;       // col 9  (\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN)
    FILETIME     shadowTime{};     // col 10 (snapshot creation time)

    bool hasCurrent = false;       // true for Older File Version rows

    // Full path to the file inside the shadow copy, used by the recover action.
    std::wstring previousPath() const
    {
        std::wstring rel = folder.size() >= 2 ? folder.substr(2) : std::wstring();
        std::wstring p = shadowName + rel;
        if (!p.empty() && p.back() != L'\\') p += L'\\';
        p += filename;
        return p;
    }

    std::wstring column(int col, const DisplayOptions& d) const
    {
        switch (col)
        {
        case 0:  return filename;
        case 1:  return folder;
        case 2:  return ItemTypeText(type);
        case 3:  return hasCurrent ? FormatFileTime(currentModified, d) : L"";
        case 4:  return FormatFileTime(previousModified, d);
        case 5:  return hasCurrent ? FormatSize(currentSize) : L"";
        case 6:  return (type == ItemType::DeletedFolder) ? L"" : FormatSize(previousSize);
        case 7:  return hasCurrent ? FormatAttrs(currentAttrs) : L"";
        case 8:  return FormatAttrs(previousAttrs);
        case 9:  return shadowName;
        case 10: return FormatFileTime(shadowTime, d);
        default: return L"";
        }
    }

    // Numeric or date sort key, else the comparison falls back to text.
    long long sortKey(int col, bool& numeric) const
    {
        auto ft = [](const FILETIME& f) { return (long long)(((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime); };
        numeric = true;
        switch (col)
        {
        case 3:  return hasCurrent ? ft(currentModified) : 0;
        case 4:  return ft(previousModified);
        case 5:  return hasCurrent ? (long long)currentSize : 0;
        case 6:  return (type == ItemType::DeletedFolder) ? 0 : (long long)previousSize;
        case 10: return ft(shadowTime);
        default: numeric = false; return 0;
        }
    }
};
