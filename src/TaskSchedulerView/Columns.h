// Columns.h -- the 37 TaskSchedulerView columns.
//
// Internal column index == string-table id minus 1001, so index 0 is "Task
// Name" (1001) and index 36 is "Task Filename" (1037). The default display
// order (kDefaultOrder) and the default visible set were confirmed against the
// original's own /scomma export header. Numeric columns sort numerically and
// honour the "Align Numeric Columns To Right" option. Date columns sort by
// their stored FILETIME.
#pragma once
#include <windows.h>
#include <commctrl.h>
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
    { L"Task Name",                     170, CK_TEXT,    200 },  // 0  1001
    { L"Status",                         70, CK_TEXT,    100 },  // 1  1002
    { L"Hidden",                         55, CK_TEXT,    100 },  // 2  1003
    { L"Last Task Result",               95, CK_NUMERIC, 100 },  // 3  1004
    { L"Last Run",                      120, CK_DATE,    100 },  // 4  1005
    { L"Next Run",                      120, CK_DATE,    100 },  // 5  1006
    { L"Missed Runs",                    80, CK_NUMERIC, 100 },  // 6  1007
    { L"Task Folder",                   170, CK_TEXT,    200 },  // 7  1008
    { L"Executable File",               200, CK_TEXT,    200 },  // 8  1009
    { L"Executable Arguments",          150, CK_TEXT,    150 },  // 9  1010
    { L"Author",                        120, CK_TEXT,    150 },  // 10 1011
    { L"Description",                   200, CK_TEXT,    250 },  // 11 1012
    { L"Source",                        120, CK_TEXT,    150 },  // 12 1013
    { L"Run On Boot",                    75, CK_TEXT,    100 },  // 13 1014
    { L"Run On Logon",                   80, CK_TEXT,    100 },  // 14 1015
    { L"Run Daily",                     100, CK_TEXT,    100 },  // 15 1016
    { L"Run Weekly",                    160, CK_TEXT,    150 },  // 16 1017
    { L"Run Monthly",                   160, CK_TEXT,    150 },  // 17 1018
    { L"COM Handler Filename",          200, CK_TEXT,    200 },  // 18 1019
    { L"COM Handler Description",       180, CK_TEXT,    200 },  // 19 1020
    { L"COM Handler Class",             200, CK_TEXT,    200 },  // 20 1021
    { L"Action Types",                   90, CK_TEXT,    100 },  // 21 1022
    { L"Run On Event",                  220, CK_TEXT,    250 },  // 22 1023
    { L"Triggers",                      110, CK_TEXT,    120 },  // 23 1024
    { L"Task Owner",                    140, CK_TEXT,    150 },  // 24 1025
    { L"Priority",                       55, CK_NUMERIC, 100 },  // 25 1026
    { L"Running Instances",              95, CK_NUMERIC, 100 },  // 26 1027
    { L"Running User Account",          140, CK_TEXT,    150 },  // 27 1028
    { L"Highest Privileges Run",        120, CK_TEXT,    100 },  // 28 1029
    { L"Start Directory",               150, CK_TEXT,    150 },  // 29 1030
    { L"Allow Demand Start",            100, CK_TEXT,    100 },  // 30 1031
    { L"Run Only If Network Available", 150, CK_TEXT,    100 },  // 31 1032
    { L"Multi-Instances Policy",        120, CK_TEXT,    120 },  // 32 1033
    { L"Wake To Run",                    75, CK_TEXT,    100 },  // 33 1034
    { L"Task File Created Time",        120, CK_DATE,    100 },  // 34 1035
    { L"Task File Modified Time",       120, CK_DATE,    100 },  // 35 1036
    { L"Task Filename",                 300, CK_TEXT,    300 },  // 36 1037
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Default display order (internal indices), byte-for-byte the order of the
// original's /scomma header line.
inline constexpr int kDefaultOrder[] = {
    0, 1, 2, 3, 4, 5, 30, 23, 13, 14, 22, 15, 16, 17, 31, 33, 32, 6, 7, 21,
    8, 9, 29, 18, 19, 20, 27, 28, 10, 11, 12, 24, 25, 26, 34, 35, 36,
};
static_assert(std::size(kDefaultOrder) == kColumnCount, "default order must list every column");
