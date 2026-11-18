// Columns.h -- the six SimpleProgramDebugger list columns.
//
// Internal column index == string-table id minus 1001, so index 0 is "Event
// Time" (1001) and index 5 is "More Information" (1006). All six are visible by
// default in this order, matching the original. Default widths were read from
// the original's own list header (120, 120, 100, 100, 200, 200).
#pragma once
#include <windows.h>
#include <array>

enum class ColKind { CK_TEXT, CK_TIME, CK_NUMERIC };

struct ColumnDef
{
    const wchar_t* title;
    int            width;   // default ListView width
    ColKind        kind;
};

// Indexed by internal column id (string id - 1001).
inline constexpr ColumnDef kColumns[] = {
    { L"Event Time",       120, ColKind::CK_TIME    },  // 0  1001
    { L"Event Type",       120, ColKind::CK_TEXT    },  // 1  1002
    { L"Thread ID",        100, ColKind::CK_NUMERIC },  // 2  1003
    { L"Memory Address",   100, ColKind::CK_TEXT    },  // 3  1004
    { L"Filename",         200, ColKind::CK_TEXT    },  // 4  1005
    { L"More Information",  200, ColKind::CK_TEXT    },  // 5  1006
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Default display order (internal indices), all columns in natural order.
inline constexpr int kDefaultOrder[] = { 0, 1, 2, 3, 4, 5 };
static_assert(std::size(kDefaultOrder) == kColumnCount, "default order must list every column");

// Event-type names, indexed by WaitForDebugEvent dwDebugEventCode (1..8).
// String ids 1501..1508. Index 0 is unused.
inline constexpr const wchar_t* kEventTypeNames[] = {
    L"",               // 0  (unused)
    L"Exception",      // 1  EXCEPTION_DEBUG_EVENT
    L"Create Thread",  // 2  CREATE_THREAD_DEBUG_EVENT
    L"Create Process", // 3  CREATE_PROCESS_DEBUG_EVENT
    L"Exit Thread",    // 4  EXIT_THREAD_DEBUG_EVENT
    L"Exit Process",   // 5  EXIT_PROCESS_DEBUG_EVENT
    L"Load DLL",       // 6  LOAD_DLL_DEBUG_EVENT
    L"Unload Dll",     // 7  UNLOAD_DLL_DEBUG_EVENT
    L"Debug String",   // 8  OUTPUT_DEBUG_STRING_EVENT
};
