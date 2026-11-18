// Columns.h -- the nine MIMEView columns, titles and field order exactly as
// recovered from sub_4016AC and the "Choose Columns" title table in the binary.
#pragma once
#include <windows.h>
#include <commctrl.h>

struct ColumnDef
{
    const wchar_t* title;
    int            width;   // original default width is 120 for every column
    int            format;  // LVCFMT_*
};

inline constexpr ColumnDef kColumns[] = {
    { L"Content Type",   150, LVCFMT_LEFT },
    { L"Extension",       80, LVCFMT_LEFT },
    { L"Plugin Name",    120, LVCFMT_LEFT },
    { L"Plugin Version",  90, LVCFMT_LEFT },
    { L"Plugin ProgID",  120, LVCFMT_LEFT },
    { L"Plugin File",    120, LVCFMT_LEFT },
    { L"Plugin CLSID",   230, LVCFMT_LEFT },
    { L"File Dialog",     70, LVCFMT_LEFT },
    { L"Open Command",   200, LVCFMT_LEFT },
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
