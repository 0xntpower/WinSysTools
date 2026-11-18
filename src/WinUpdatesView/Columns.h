// Columns.h -- the 17 WinUpdatesView columns, in the exact display order, titles
// and default widths used by the original (confirmed against its /stext output
// and the saved Columns blob: widths 150 150 120 120 120 120 250 250 250 200
// 150 150 150 110 110 120 120).
#pragma once
#include <windows.h>
#include <commctrl.h>

struct ColumnDef
{
    const wchar_t* title;
    int            width;      // ListView default width (saved Columns blob)
    int            format;     // LVCFMT_*
    int            htmlWidth;  // per-column width used in the HTML report
};

inline constexpr ColumnDef kColumns[] = {
    { L"Title",                 150, LVCFMT_LEFT, 250 },  // 0
    { L"Description",           150, LVCFMT_LEFT, 700 },  // 1
    { L"KB Number",             120, LVCFMT_LEFT, 100 },  // 2
    { L"Install Date",          120, LVCFMT_LEFT, 100 },  // 3
    { L"Update Operation",      120, LVCFMT_LEFT, 100 },  // 4
    { L"Operation Result",      120, LVCFMT_LEFT, 100 },  // 5
    { L"Information URL",        250, LVCFMT_LEFT, 150 },  // 6
    { L"Support URL",           250, LVCFMT_LEFT, 150 },  // 7
    { L"Uninstall Notes",       250, LVCFMT_LEFT, 700 },  // 8
    { L"Category",              200, LVCFMT_LEFT, 200 },  // 9
    { L"Client Application ID", 150, LVCFMT_LEFT, 100 },  // 10
    { L"Service ID",            150, LVCFMT_LEFT, 100 },  // 11
    { L"Update ID",             150, LVCFMT_LEFT, 100 },  // 12
    { L"Revision Number",       110, LVCFMT_LEFT, 100 },  // 13
    { L"Unmapped Result Code",  110, LVCFMT_LEFT, 100 },  // 14
    { L"Server Selection",      120, LVCFMT_LEFT, 100 },  // 15
    { L"hResult",               120, LVCFMT_LEFT, 100 },  // 16
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
