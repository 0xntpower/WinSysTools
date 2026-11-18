// Records.h -- one shadow copy snapshot and one file entry, each with its display
// cells precomputed. Sorting uses the typed keys here, numeric for the size column
// and FILETIME for the date columns, text for the rest.
#pragma once
#include <windows.h>
#include <string>
#include <array>
#include "Columns.h"

struct DisplayOptions
{
    bool showTimeInGmt = false; // Options -> Show Time In GMT
};

struct SnapshotRecord
{
    std::array<std::wstring, kSnapColumnCount> cells;

    // kept for file browsing and the /CopyFile matcher
    std::wstring deviceObject; // \\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN
    std::wstring volumePath;   // mount point such as C: drive
    std::wstring explorerPath; // \\localhost\X$\@GMT-...
    long long creationKey = 0; // creation FILETIME as a sort key

    const std::wstring& column(int c) const
    {
        static const std::wstring empty;
        return (c >= 0 && c < kSnapColumnCount) ? cells[c] : empty;
    }

    long long sortKey(int c, bool& numeric) const
    {
        numeric = true;
        if (c == 6)
        {
            return creationKey;
        }
        numeric = false;
        return 0;
    }
};

struct FileRecord
{
    std::array<std::wstring, kFileColumnCount> cells;

    std::wstring name; // raw file name, for navigation and copy
    bool isDir = false;
    long long sizeKey = 0;
    long long modifiedKey = 0;
    long long createdKey = 0;
    long long entryModifiedKey = 0;

    const std::wstring& column(int c) const
    {
        static const std::wstring empty;
        return (c >= 0 && c < kFileColumnCount) ? cells[c] : empty;
    }

    long long sortKey(int c, bool& numeric) const
    {
        numeric = true;
        switch (c)
        {
        case 1:
            return modifiedKey;
        case 2:
            return createdKey;
        case 3:
            return sizeKey;
        case 6:
            return entryModifiedKey;
        default:
            numeric = false;
            return 0;
        }
    }
};
