// Columns.h -- the 13 CredentialsFileView columns.
//
// Internal column index == string-table id minus 1001, so index 0 is
// "Filename" (1001) and index 12 is "File Created Time" (1013). The order and
// titles match the original's upper-pane table and its /scomma export header.
// Numeric columns (sizes) sort numerically, date columns by their FILETIME.
#pragma once
#include <windows.h>
#include <array>

enum class ColKind { CK_TEXT, CK_NUMERIC, CK_DATE };

struct ColumnDef
{
    const wchar_t* title;
    int            width;      // default ListView width
    ColKind        kind;
};

// Indexed by internal column id (string id - 1001).
inline constexpr ColumnDef kColumns[] = {
    { L"Filename",           130, ColKind::CK_TEXT    },  // 0  1001
    { L"Version",             85, ColKind::CK_TEXT    },  // 1  1002
    { L"Decrypted Size",      90, ColKind::CK_NUMERIC },  // 2  1003
    { L"Modified Time",      130, ColKind::CK_DATE    },  // 3  1004
    { L"Entry Type",         100, ColKind::CK_TEXT    },  // 4  1005
    { L"Persist",             85, ColKind::CK_TEXT    },  // 5  1006
    { L"Entry Name",         200, ColKind::CK_TEXT    },  // 6  1007
    { L"User Name",          130, ColKind::CK_TEXT    },  // 7  1008
    { L"Password",           130, ColKind::CK_TEXT    },  // 8  1009
    { L"Full Path",          240, ColKind::CK_TEXT    },  // 9  1010
    { L"File Size",           80, ColKind::CK_NUMERIC },  // 10 1011
    { L"File Modified Time",  130, ColKind::CK_DATE   },  // 11 1012
    { L"File Created Time",   130, ColKind::CK_DATE   },  // 12 1013
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Default display order: every column in declared order, all visible.
inline constexpr int kDefaultOrder[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
static_assert(std::size(kDefaultOrder) == kColumnCount, "default order must list every column");
