// Columns.h -- the seven HeapMemView columns, in the default display order the
// original uses (the order the record getter sub_140001E24 enforces).
//
// Column titles come from STRINGTABLE 1001..1007, but the display order is not the
// numeric string order. Each display index returns one record field, and the setup
// gives it a title. The mapping below is the only one consistent with the getter.
#pragma once
#include <windows.h>
#include <commctrl.h>

struct ColumnDef
{
    const wchar_t* title;
    int width;  // default pixel width
    int format; // LVCFMT_*
};

inline constexpr ColumnDef kColumns[] = {
    {L"Data Address", 120, LVCFMT_LEFT},   // 0  header + 16
    {L"Header Address", 120, LVCFMT_LEFT}, // 1  header
    {L"Block Size", 80, LVCFMT_RIGHT},     // 2  rawSize - 16
    {L"Heap Handle", 120, LVCFMT_LEFT},    // 3  heap
    {L"Segment Index", 90, LVCFMT_RIGHT},  // 4  segIndex
    {L"New", 40, LVCFMT_CENTER},           // 5  isNew -> "*"
    {L"Data Preview", 220, LVCFMT_LEFT},   // 6  data bytes preview
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
