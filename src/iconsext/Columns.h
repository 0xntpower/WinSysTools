// Columns.h -- the six main-list columns, titles and widths exactly as recovered
// from sub_4051F2 (InsertColumn with string ids 1101..1106) and the on-disk
// iconsext.cfg "Columns" blob (widths 150,70,120,60,100,300).
#pragma once
#include <windows.h>
#include <commctrl.h>

struct ColumnDef
{
    const wchar_t* title;
    int            width;
    int            format;  // LVCFMT_*
};

// Display order matches the original's column insertion order. Note that the
// original's visible order is Resource, Images, Library, Type, Total Size,
// Filename -- Total Size (string 1106) is inserted before Filename (1105).
inline constexpr ColumnDef kColumns[] = {
    { L"Resource",   150, LVCFMT_LEFT },   // 0  "basename(id)"
    { L"Images",      70, LVCFMT_LEFT },   // 1  number of images in the group
    { L"Library",    120, LVCFMT_LEFT },   // 2  file basename
    { L"Type",        60, LVCFMT_LEFT },   // 3  "Icon" / "Cursor"
    { L"Total Size", 100, LVCFMT_LEFT },   // 4  sum of bytesInRes
    { L"Filename",   300, LVCFMT_LEFT },   // 5  full file path
};

inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));
