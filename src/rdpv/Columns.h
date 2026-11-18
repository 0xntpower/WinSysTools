// Columns.h -- the five RemoteDesktopPassView columns, titles and order exactly as
// recovered from the STRINGTABLE (1001 "Computer Name", 1002 "Domain",
// 1003 "User Name", 1004 "Password", 1005 "Filename").
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
    { L"Computer Name", 120, LVCFMT_LEFT },  // string 1001 -> "full address"
    { L"Domain",         90, LVCFMT_LEFT },  // string 1002 -> "domain"
    { L"User Name",     110, LVCFMT_LEFT },  // string 1003 -> "username"
    { L"Password",      110, LVCFMT_LEFT },  // string 1004 -> recovered plaintext
    { L"Filename",      220, LVCFMT_LEFT },  // string 1005 -> source .rdp path
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
