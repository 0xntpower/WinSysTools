// DbgRow.h -- one captured debug line as the list keeps it.
// The capture thread fills ft (a UTC FILETIME taken when the message arrived),
// qpc (a QueryPerformanceCounter reading for the elapsed-time column), pid and
// message. The sequence number is assigned by the window when the row lands, so
// numbering stays dense after a clear. Column cells are formatted on demand by
// the window, because the time column depends on live view options.
#pragma once
#include <windows.h>
#include <string>

struct DbgRow
{
    unsigned seq = 0;
    FILETIME ft{0, 0};    // UTC file time at capture
    long long qpc = 0;    // performance counter at capture
    unsigned int pid = 0; // writer process id, from buffer offset 0
    std::wstring message; // the OutputDebugString payload
};
