// Columns.h -- the seven ProductKeyScanner columns, titles and order exactly as
// recovered from the STRINGTABLE (ids 1001..1007) and the length-prefixed title
// array at .rsrc 0x140023100. Default widths are the recreation's own sensible
// values (the original keeps them in its "Columns" config blob).
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
    { L"Product ID",     120, LVCFMT_LEFT },  // 1001
    { L"Product Key",    190, LVCFMT_LEFT },  // 1002
    { L"Product Name",   170, LVCFMT_LEFT },  // 1003
    { L"Scan Type",       70, LVCFMT_LEFT },  // 1004
    { L"Registry Key",   300, LVCFMT_LEFT },  // 1005
    { L"Registry Value", 110, LVCFMT_LEFT },  // 1006
    { L"Modified Time",  130, LVCFMT_LEFT },  // 1007
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
