// Columns.h -- the 15 OfficeIns columns, titles and display order exactly as recovered.
//
// The display order is the one the record getter sub_140001BC4 enforces: for each
// column index it returns a specific record field, and the column-setup gives each
// index a title from the STRINGTABLE (1001..1015) -- but NOT in numeric order. The
// mapping below is the only assignment consistent with that getter.
//
//   idx  title (string id)        record field returned by sub_140001BC4
//   0    ProgID          (1001)   progId
//   1    Description     (1002)   description (Description, else FriendlyName)
//   2    Software        (1003)   Office application name (Word..FrontPage)
//   3    Start Mode      (1013)   LoadBehavior -> Disabled/Startup/Demand/FirstTime
//   4    Connected       (1014)   LoadBehavior bit 0 -> Yes/No
//   5    Product Name    (1004)   version info ProductName
//   6    Version         (1005)   version info FileVersion
//   7    Company         (1006)   version info CompanyName
//   8    File Description(1007)   version info FileDescription
//   9    File Created Time(1008)  file creation time (local)
//   10   Filename        (1009)   resolved InprocServer32 / LocalServer32 path
//   11   CLSID           (1010)   add-in CLSID
//   12   .Net Add-In     (1011)   mscoree.dll host -> Yes/No
//   13   .NET Version    (1015)   managed RuntimeVersion
//   14   Registry Key    (1012)   HKCU/HKLM \ Addins subkey \ ProgID
#pragma once
#include <windows.h>
#include <commctrl.h>

struct ColumnDef
{
    const wchar_t* title;
    int            width;   // default 100, from OfficeIns.cfg
    int            format;  // LVCFMT_*
};

inline constexpr ColumnDef kColumns[] = {
    { L"ProgID",            100, LVCFMT_LEFT },  // 0
    { L"Description",       100, LVCFMT_LEFT },  // 1
    { L"Software",          100, LVCFMT_LEFT },  // 2
    { L"Start Mode",        100, LVCFMT_LEFT },  // 3
    { L"Connected",         100, LVCFMT_LEFT },  // 4
    { L"Product Name",      100, LVCFMT_LEFT },  // 5
    { L"Version",           100, LVCFMT_LEFT },  // 6
    { L"Company",           100, LVCFMT_LEFT },  // 7
    { L"File Description",  100, LVCFMT_LEFT },  // 8
    { L"File Created Time", 100, LVCFMT_LEFT },  // 9
    { L"Filename",          100, LVCFMT_LEFT },  // 10
    { L"CLSID",             100, LVCFMT_LEFT },  // 11
    { L".Net Add-In",       100, LVCFMT_LEFT },  // 12
    { L".NET Version",      100, LVCFMT_LEFT },  // 13
    { L"Registry Key",      100, LVCFMT_LEFT },  // 14
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Fixed character widths the /stabular export uses per column index (measured from
// the original output). Each value is truncated to width-1 and left-padded to width,
// and the tabular format has no header row.
inline constexpr int kTabularWidth[kColumnCount] = {
    51, 81, 31, 21, 11, 51, 51, 51, 51, 31, 121, 41, 11, 11, 61
};
