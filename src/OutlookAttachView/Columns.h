// Columns.h -- the 22 OutlookAttachView columns.
//
// Internal column index == string-table id minus 1001, so index 0 is
// "Filename" (1001) and index 21 is "Hash" (1022). Titles and order were
// recovered from the original binary string table (ids 1001..1022). Numeric
// columns sort numerically and honour the "Align Numeric Columns To Right"
// option. Date columns sort by their stored FILETIME.
#pragma once
#include <windows.h>
#include <array>

enum ColKind { CK_TEXT, CK_NUMERIC, CK_DATE };

struct ColumnDef
{
    const wchar_t* title;
    int            width;      // default ListView width
    ColKind        kind;
    int            htmlWidth;  // per-column width used in the HTML report
};

// Indexed by internal column id (string id - 1001).
inline constexpr ColumnDef kColumns[] = {
    { L"Filename",              160, CK_TEXT,    150 },  // 0  1001
    { L"From",                  130, CK_TEXT,    150 },  // 1  1002
    { L"To",                    130, CK_TEXT,    150 },  // 2  1003
    { L"Subject",               160, CK_TEXT,    200 },  // 3  1004
    { L"MIME Type",             100, CK_TEXT,    120 },  // 4  1005
    { L"Attachment Type",       100, CK_TEXT,    110 },  // 5  1006
    { L"Created On",            120, CK_DATE,    120 },  // 6  1007
    { L"File Size",              80, CK_NUMERIC, 100 },  // 7  1008
    { L"Attachment ID",          90, CK_NUMERIC, 100 },  // 8  1009
    { L"Inline",                 50, CK_TEXT,     80 },  // 9  1010
    { L"Folder Name",           110, CK_TEXT,    120 },  // 10 1011
    { L"Folder Path",           160, CK_TEXT,    200 },  // 11 1012
    { L"Computer Address",      140, CK_TEXT,    150 },  // 12 1013
    { L"Email Software",        130, CK_TEXT,    150 },  // 13 1014
    { L"Extension",              70, CK_TEXT,    100 },  // 14 1015
    { L"From Email",            150, CK_TEXT,    170 },  // 15 1016
    { L"To Email",              150, CK_TEXT,    170 },  // 16 1017
    { L"Message Direction",     110, CK_TEXT,    120 },  // 17 1018
    { L"Domain",                110, CK_TEXT,    120 },  // 18 1019
    { L"Message EntryID",       180, CK_TEXT,    200 },  // 19 1020
    { L"Message Delivery Time", 120, CK_DATE,    120 },  // 20 1021
    { L"Hash",                  150, CK_TEXT,    170 },  // 21 1022
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Default display order and default visible set (all columns, in id order).
inline constexpr int kDefaultOrder[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
};
static_assert(std::size(kDefaultOrder) == kColumnCount, "default order must list every column");
