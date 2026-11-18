// Columns.h -- the two list panes' columns. The upper pane lists Volume Shadow
// Copy snapshots (13 columns, STRINGTABLE 1001..1013), the lower pane lists the
// files inside the selected snapshot (7 columns, STRINGTABLE 1101..1107). The
// array index is the field index used throughout, matching the original.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <iterator>

enum class ColKind
{
    CK_TEXT,
    CK_NUMERIC,
    CK_DATE
};

struct ColumnDef
{
    const wchar_t* title;
    int width;
    int format; // LVCFMT_*
    ColKind kind;
};

// Upper pane -- shadow copy snapshots. Fields come from VSS_SNAPSHOT_PROP, with
// the Explorer Path and Volume Path derived the way the original derives them.
inline constexpr ColumnDef kSnapColumns[] = {
    {L"Snapshot Name", 330, LVCFMT_LEFT,
     ColKind::CK_TEXT}, // 0  device object \\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN
    {L"Explorer Path", 170, LVCFMT_LEFT, ColKind::CK_TEXT},       // 1  \\localhost\X$\@GMT-...
    {L"Volume Path", 80, LVCFMT_LEFT, ColKind::CK_TEXT},          // 2  mount point such as C: drive
    {L"Volume Name", 270, LVCFMT_LEFT, ColKind::CK_TEXT},         // 3  volume GUID name
    {L"Originating Machine", 150, LVCFMT_LEFT, ColKind::CK_TEXT}, // 4
    {L"Service Machine", 150, LVCFMT_LEFT, ColKind::CK_TEXT},     // 5
    {L"Creation Time", 130, LVCFMT_LEFT, ColKind::CK_DATE},       // 6
    {L"Attributes", 180, LVCFMT_LEFT, ColKind::CK_TEXT},          // 7
    {L"Exposed Name", 100, LVCFMT_LEFT, ColKind::CK_TEXT},        // 8
    {L"Exposed Path", 100, LVCFMT_LEFT, ColKind::CK_TEXT},        // 9
    {L"Snapshot ID", 250, LVCFMT_LEFT, ColKind::CK_TEXT},         // 10
    {L"SnapshotSet ID", 250, LVCFMT_LEFT, ColKind::CK_TEXT},      // 11
    {L"Provider ID", 250, LVCFMT_LEFT, ColKind::CK_TEXT},         // 12
};
inline constexpr int kSnapColumnCount = static_cast<int>(std::size(kSnapColumns));

// Lower pane -- files and folders inside the selected snapshot.
inline constexpr ColumnDef kFileColumns[] = {
    {L"Filename", 220, LVCFMT_LEFT, ColKind::CK_TEXT},            // 0
    {L"Modified Time", 130, LVCFMT_LEFT, ColKind::CK_DATE},       // 1
    {L"Created Time", 130, LVCFMT_LEFT, ColKind::CK_DATE},        // 2
    {L"File Size", 90, LVCFMT_RIGHT, ColKind::CK_NUMERIC},        // 3
    {L"Attributes", 90, LVCFMT_LEFT, ColKind::CK_TEXT},           // 4
    {L"File Extension", 90, LVCFMT_LEFT, ColKind::CK_TEXT},       // 5
    {L"Entry Modified Time", 130, LVCFMT_LEFT, ColKind::CK_DATE}, // 6
};
inline constexpr int kFileColumnCount = static_cast<int>(std::size(kFileColumns));

// All columns are visible by default, in table order.
inline constexpr int kSnapColumnField = 1001; // STRINGTABLE base for reference
inline constexpr int kFileColumnField = 1101;
