// Columns.h -- the three VNCPassView columns, titles and order exactly as recovered
// from the column descriptor table at 0x40b990 (order 1001, 1003, 1002) and the
// STRINGTABLE (1001 "Password Type", 1002 "Password", 1003 "Version").
#pragma once
#include <windows.h>
#include <commctrl.h>

struct ColumnDef
{
    const wchar_t* title;
    int            width;
    int            format;  // LVCFMT_*
};

inline constexpr ColumnDef kColumns[] = {
    { L"Password Type", 110, LVCFMT_LEFT },  // string 1001 -> System / Current User
    { L"Version",        70, LVCFMT_LEFT },  // string 1003 -> itoa(version code)
    { L"Password",      190, LVCFMT_LEFT },  // string 1002 -> decrypted password
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
