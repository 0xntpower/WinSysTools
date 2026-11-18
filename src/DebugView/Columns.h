// Columns.h -- the four list columns the recreation shows.
// The original draws a custom list whose visible fields are a line number, a
// time stamp and the debug output. This recreation uses a report ListView and
// adds the process id as its own column, since the captured DBWIN record
// carries the writer pid in the first four bytes of the shared buffer.
#pragma once
#include <windows.h>
#include <array>

enum ColId
{
    COL_SEQ = 0,
    COL_TIME = 1,
    COL_PID = 2,
    COL_MESSAGE = 3
};

struct ColumnDef
{
    const wchar_t* title;
    int width;
    int format; // LVCFMT_LEFT or LVCFMT_RIGHT
};

inline constexpr ColumnDef kColumns[] = {
    {L"#", 52, LVCFMT_RIGHT},           // sequence number
    {L"Time", 95, LVCFMT_LEFT},         // clock time or elapsed seconds
    {L"PID", 56, LVCFMT_RIGHT},         // writer process id
    {L"Debug Print", 760, LVCFMT_LEFT}, // the OutputDebugString message
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
