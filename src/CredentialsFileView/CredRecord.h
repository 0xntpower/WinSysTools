// CredRecord.h -- one Credentials entry with all 13 display columns precomputed.
// The cells are filled by CredFile from the decrypted DPAPI structure plus the
// file metadata. Sorting uses the typed keys here, numeric for the size columns
// and FILETIME for the three date columns. The decrypted bytes themselves live
// in `plain` and feed the lower pane hex dump or strings view.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <array>
#include "Columns.h"

struct DisplayOptions
{
    bool showTimeInGmt = false; // Options -> Show Time In GMT (cfg ShowTimeInGMT)
};

struct CredRecord
{
    std::array<std::wstring, kColumnCount> cells;
    std::vector<BYTE> plain; // decrypted entry bytes, for the lower pane

    // typed sort keys
    long long modifiedKey = 0;     // col 3
    long long fileModifiedKey = 0; // col 11
    long long fileCreatedKey = 0;  // col 12
    long long decryptedSize = 0;   // col 2
    long long fileSize = 0;        // col 10

    const std::wstring& column(int col) const
    {
        static const std::wstring empty;
        return (col >= 0 && col < kColumnCount) ? cells[col] : empty;
    }

    long long sortKey(int col, bool& numeric) const
    {
        numeric = true;
        switch (col)
        {
        case 2:
            return decryptedSize;
        case 3:
            return modifiedKey;
        case 10:
            return fileSize;
        case 11:
            return fileModifiedKey;
        case 12:
            return fileCreatedKey;
        default:
            numeric = false;
            return 0;
        }
    }
};
