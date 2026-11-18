// AttachRecord.h -- one Outlook attachment as the original lays it out, with
// all 22 display columns precomputed. The cell strings are built in MapiScanner
// from the Extended MAPI message and attachment properties. Sorting uses the
// typed keys here, numeric for the size and id columns and FILETIME for dates.
#pragma once
#include <windows.h>
#include <string>
#include <array>
#include "Columns.h"

struct DisplayOptions
{
    bool showTimeInGmt = false;   // Options -> Show Time In GMT
    int  fileSizeUnit = 1;        // 1 Bytes, 2 KB, 3 MB, 4 GB, 5 Automatic
};

struct AttachRecord
{
    std::array<std::wstring, kColumnCount> cells;

    // identity for save or delete operations
    std::wstring storeEntryId;    // PR_ENTRYID of the message store, as a hex string
    std::wstring msgEntryId;      // PR_ENTRYID of the message, as a hex string
    unsigned int attachNum = 0;   // PR_ATTACH_NUM

    // typed sort keys (filled alongside the cells)
    long long createdKey = 0;
    long long deliveryKey = 0;
    unsigned long long sizeBytes = 0;
    int attachIdKey = 0;
    bool inlineAttachment = false;

    // display text for a column (internal index)
    const std::wstring& column(int col) const
    {
        static const std::wstring empty;
        return (col >= 0 && col < kColumnCount) ? cells[col] : empty;
    }

    // sort key: numeric and date columns compare numerically, the rest by text
    long long sortKey(int col, bool& numeric) const
    {
        numeric = true;
        switch (col)
        {
        case 6:  return createdKey;
        case 7:  return (long long)sizeBytes;
        case 8:  return attachIdKey;
        case 20: return deliveryKey;
        default: numeric = false; return 0;
        }
    }
};
