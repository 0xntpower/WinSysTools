// TaskRecord.h -- one scheduled task as the original lays it out, with all 37
// display columns precomputed. The column strings are built in TaskEnumerator
// from the Task Scheduler 2.0 COM objects (and, for the COM-handler columns and
// the file columns, from the registry and the task file). Sorting uses the
// typed keys here, numeric for the numeric columns and FILETIME for the dates.
#pragma once
#include <windows.h>
#include <string>
#include <array>
#include "Columns.h"

struct DisplayOptions
{
    bool showTimeInGmt = false;   // Options -> Show Time In GMT (cfg ShowTimeInGMT)
};

struct TaskRecord
{
    std::array<std::wstring, kColumnCount> cells;
    std::wstring taskPath;   // Task Scheduler path (folder + name), for enable/disable/run

    // typed sort keys (filled alongside the cells)
    long long lastRunKey = 0;
    long long nextRunKey = 0;
    long long fileCreatedKey = 0;
    long long fileModifiedKey = 0;
    unsigned int lastResult = 0;
    int missedRuns = 0;
    int priority = 0;
    int runningInstances = 0;

    // display text for a column (internal index)
    const std::wstring& column(int col) const
    {
        static const std::wstring empty;
        return (col >= 0 && col < kColumnCount) ? cells[col] : empty;
    }

    // sort key: numeric and date columns compare numerically, the rest by text
    long long sortKey(int col, bool& numeric) const
    {
        numeric = true;
        switch (col)
        {
        case 3:  return lastResult;
        case 6:  return missedRuns;
        case 25: return priority;
        case 26: return runningInstances;
        case 4:  return lastRunKey;
        case 5:  return nextRunKey;
        case 34: return fileCreatedKey;
        case 35: return fileModifiedKey;
        default: numeric = false; return 0;
        }
    }
};
