// IconSave.h -- the .ICO / .CUR writer and the folder-save loop.
// Faithful reconstruction of the original save core (sub_401807 / sub_405779 /
// sub_405646), cross-checked against rea/Ghidra. The writer rebuilds an ICONDIR
// from the module's RT_GROUP_ICON / RT_GROUP_CURSOR directory and appends each
// RT_ICON / RT_CURSOR member image, exactly as the original does.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "IconEntry.h"

// Write a single icon/cursor group from a loaded module into an already-open file.
// srcIsCursor selects the resource family (RT_GROUP_CURSOR/RT_CURSOR vs
// RT_GROUP_ICON/RT_ICON). outType is the ICONDIR type written: 1 = .ico, 2 = .cur.
// Saving a cursor as .ico (-asico) passes srcIsCursor=true, outType=1.
[[nodiscard]] bool WriteIconToFile(HANDLE hFile, HMODULE hModule, const wchar_t* resName,
                     bool srcIsCursor, int outType);

// Open (create/truncate) path and write one group. Returns false on any failure.
[[nodiscard]] bool SaveIconToPath(const std::wstring& path, HMODULE hModule, const wchar_t* resName,
                    bool srcIsCursor, int outType);

// Build the per-icon output filename the original uses (sub_405646):
//   <basename-without-ext>_<id><ext>, colliding as <base>_<n>_<id><ext>.
// folder is the destination directory. Returns the full path, or empty on failure.
[[nodiscard]] std::wstring BuildSavePath(const std::wstring& folder, const IconEntry& e, bool asCur);

struct SaveStats { int saved = 0; int failed = 0; };

// Save the selected entries to folder. saveCursorsAsIco maps to the -asico option:
// when true, cursors are written as .ico, otherwise as .cur.
SaveStats SaveEntries(const std::wstring& folder, const std::vector<IconEntry>& entries,
                      bool saveCursorsAsIco);
