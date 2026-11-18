// Columns.h -- the 11 PreviousFilesRecovery columns.
//
// Internal column index equals the string-table id minus 1001, so index 0 is
// "Filename" (1001) and index 10 is "Shadow Copy Time" (1011). The field each
// column shows was recovered from the column-text builder in the binary
// (sub_1400024CC). Size columns sort numerically, the time columns sort by their
// stored FILETIME, the rest by text.
#pragma once
#include <windows.h>
#include <array>

enum class ColKind
{
    CK_TEXT,
    CK_NUMERIC,
    CK_DATE
};

struct ColumnDef
{
    const wchar_t* title;
    int width; // default ListView width in pixels
    ColKind kind;
};

// Indexed by internal column id (string id - 1001).
inline constexpr ColumnDef kColumns[] = {
    {L"Filename", 160, ColKind::CK_TEXT},                 // 0  1001
    {L"Folder", 240, ColKind::CK_TEXT},                   // 1  1002
    {L"Item Type", 110, ColKind::CK_TEXT},                // 2  1003
    {L"Current Modified Time", 130, ColKind::CK_DATE},    // 3  1004
    {L"Previous Modified Time", 130, ColKind::CK_DATE},   // 4  1005
    {L"Current File Size", 95, ColKind::CK_NUMERIC},      // 5  1006
    {L"Previous File Size", 95, ColKind::CK_NUMERIC},     // 6  1007
    {L"Current File Attributes", 120, ColKind::CK_TEXT},  // 7  1008
    {L"Previous File Attributes", 120, ColKind::CK_TEXT}, // 8  1009
    {L"Shadow Copy Name", 260, ColKind::CK_TEXT},         // 9  1010
    {L"Shadow Copy Time", 130, ColKind::CK_DATE},         // 10 1011
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Default display order is the natural column order, matching the original's
// own /scomma header line.
inline constexpr int kDefaultOrder[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static_assert(std::size(kDefaultOrder) == kColumnCount, "default order lists every column");
