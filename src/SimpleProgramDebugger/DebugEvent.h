// DebugEvent.h -- one logged debug event as the list shows it, with all six
// display columns precomputed by the debug thread. The cells are built from the
// raw DEBUG_EVENT in Debugger.cpp. timeKey (the event FILETIME) and tid give the
// typed sort keys for the Event Time and Thread ID columns.
#pragma once
#include <windows.h>
#include <string>
#include <array>
#include "Columns.h"

struct DbgRow
{
    std::array<std::wstring, kColumnCount> cells;
    long long    timeKey = 0;   // event FILETIME in 100ns units (Event Time sort)
    unsigned int tid     = 0;   // thread id (Thread ID numeric sort)

    const std::wstring& column(int c) const
    {
        static const std::wstring empty;
        return (c >= 0 && c < kColumnCount) ? cells[c] : empty;
    }

    long long sortKey(int col, bool& numeric) const
    {
        numeric = true;
        switch (col)
        {
        case 0:  return timeKey;
        case 2:  return tid;
        default: numeric = false; return 0;
        }
    }
};
