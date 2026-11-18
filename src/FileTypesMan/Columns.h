// Columns.h -- the two list panes' columns, titles and widths exactly as recovered
// from the binary's static column tables (unk_140023230 upper, dword_1400235F0 lower).
// The array index is the field index the per-record getter switches on, matching the
// original. Titles come from STRINGTABLE 1001.. (upper) and 1101.. (lower). All columns
// are visible by default, in this order.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <iterator>

struct ColumnDef
{
    const wchar_t* title;
    int            width;
    int            format;  // LVCFMT_*
};

// Upper pane -- file types / extensions (20 columns).
inline constexpr ColumnDef kTypeColumns[] = {
    { L"Extension",             110, LVCFMT_LEFT },  // 0
    { L"Type Name",             130, LVCFMT_LEFT },  // 1
    { L"Description",           150, LVCFMT_LEFT },  // 2
    { L"MIME Type",             100, LVCFMT_LEFT },  // 3
    { L"Perceived Type",        110, LVCFMT_LEFT },  // 4
    { L"In New Menu",            90, LVCFMT_LEFT },  // 5
    { L"Excluded",               90, LVCFMT_LEFT },  // 6
    { L"Always Show Extension",  90, LVCFMT_LEFT },  // 7
    { L"Flags",                 100, LVCFMT_LEFT },  // 8
    { L"Browser Flags",         100, LVCFMT_LEFT },  // 9
    { L"Default Icon",          250, LVCFMT_LEFT },  // 10
    { L"Modified Time",         120, LVCFMT_LEFT },  // 11
    { L"File Type Group",        90, LVCFMT_LEFT },  // 12
    { L"User Choice",            90, LVCFMT_LEFT },  // 13
    { L"Icon Handler CLSID",    150, LVCFMT_LEFT },  // 14
    { L"Product Name",          150, LVCFMT_LEFT },  // 15
    { L"Product Version",       120, LVCFMT_LEFT },  // 16
    { L"Product Description",   150, LVCFMT_LEFT },  // 17
    { L"Company Name",          120, LVCFMT_LEFT },  // 18
    { L"Registry Key Owner",    150, LVCFMT_LEFT },  // 19
};
inline constexpr int kTypeColumnCount = static_cast<int>(std::size(kTypeColumns));

// Lower pane -- actions of the selected type (20 columns). Field 10 keeps the original's
// STRINGTABLE title (1109) and carries the DDE Topic value.
inline constexpr ColumnDef kActionColumns[] = {
    { L"Name",                     100, LVCFMT_LEFT },  // 0
    { L"Caption",                  130, LVCFMT_LEFT },  // 1
    { L"Default",                   70, LVCFMT_LEFT },  // 2
    { L"Disabled",                  70, LVCFMT_LEFT },  // 3
    { L"Last Modified",            120, LVCFMT_LEFT },  // 4
    { L"Command-Line",             350, LVCFMT_LEFT },  // 5
    { L"Icon",                     250, LVCFMT_LEFT },  // 6
    { L"DDE Message",              100, LVCFMT_LEFT },  // 7
    { L"DDE Application",          100, LVCFMT_LEFT },  // 8
    { L"DDE Ifexec",               100, LVCFMT_LEFT },  // 9
    { L"Select executable file",   100, LVCFMT_LEFT },  // 10 (DDE Topic)
    { L"Process Filename",         240, LVCFMT_LEFT },  // 11
    { L"In SystemFileAssociations",100, LVCFMT_LEFT },  // 12
    { L"Registry Key",             350, LVCFMT_LEFT },  // 13
    { L"Extended",                  80, LVCFMT_LEFT },  // 14
    { L"Product Name",             150, LVCFMT_LEFT },  // 15
    { L"Product Version",          120, LVCFMT_LEFT },  // 16
    { L"Product Description",      150, LVCFMT_LEFT },  // 17
    { L"Company Name",             150, LVCFMT_LEFT },  // 18
    { L"Registry Key Owner",       150, LVCFMT_LEFT },  // 19
};
inline constexpr int kActionColumnCount = static_cast<int>(std::size(kActionColumns));
