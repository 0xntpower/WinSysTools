// Columns.h -- the upper pane connection columns. The original stores these titles
// in STRINGTABLE 1001..1023 (23 columns). The array index is the field index used
// throughout, matching the original's column ordering. The capture-options adapter
// list columns come from STRINGTABLE 1051..1056.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <iterator>

enum ColKind { CK_TEXT, CK_NUMERIC, CK_DATE };

struct ColumnDef
{
    const wchar_t* title;
    int            width;
    int            format;   // LVCFMT_*
    ColKind        kind;
};

// Upper pane -- one row per TCP/UDP/ICMP conversation. Field order matches the
// original's STRINGTABLE 1001..1023 so the saved Columns blob stays compatible.
inline constexpr ColumnDef kColumns[] = {
    { L"Index",              50,  LVCFMT_RIGHT, CK_NUMERIC },  // 0
    { L"Protocol",           60,  LVCFMT_LEFT,  CK_TEXT    },  // 1
    { L"Local Address",      110, LVCFMT_LEFT,  CK_TEXT    },  // 2
    { L"Remote Address",     110, LVCFMT_LEFT,  CK_TEXT    },  // 3
    { L"Local Port",         70,  LVCFMT_LEFT,  CK_TEXT    },  // 4
    { L"Remote Port",        70,  LVCFMT_LEFT,  CK_TEXT    },  // 5
    { L"Packets",            60,  LVCFMT_LEFT,  CK_TEXT    },  // 6
    { L"Total Size",         80,  LVCFMT_LEFT,  CK_TEXT    },  // 7
    { L"Capture Time",       130, LVCFMT_LEFT,  CK_DATE    },  // 8
    { L"Data Size",          80,  LVCFMT_LEFT,  CK_TEXT    },  // 9
    { L"Service Name",       90,  LVCFMT_LEFT,  CK_TEXT    },  // 10
    { L"Local Host",         120, LVCFMT_LEFT,  CK_TEXT    },  // 11
    { L"Remote Host",        120, LVCFMT_LEFT,  CK_TEXT    },  // 12
    { L"ProcessID",          60,  LVCFMT_LEFT,  CK_TEXT    },  // 13
    { L"Process Filename",   150, LVCFMT_LEFT,  CK_TEXT    },  // 14
    { L"Local MAC Address",  120, LVCFMT_LEFT,  CK_TEXT    },  // 15
    { L"Remote MAC Address", 120, LVCFMT_LEFT,  CK_TEXT    },  // 16
    { L"Local IP Country",   100, LVCFMT_LEFT,  CK_TEXT    },  // 17
    { L"Remote IP Country",  100, LVCFMT_LEFT,  CK_TEXT    },  // 18
    { L"Last Packet Time",   130, LVCFMT_LEFT,  CK_DATE    },  // 19
    { L"Data Speed",         80,  LVCFMT_LEFT,  CK_TEXT    },  // 20
    { L"Process User",       100, LVCFMT_LEFT,  CK_TEXT    },  // 21
    { L"Duration",           80,  LVCFMT_LEFT,  CK_TEXT    },  // 22
};
inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
