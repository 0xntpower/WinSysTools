// Scanner.h -- the previous-files scan. Walks the live folder tree, and for each
// folder compares its files against the same folder inside every shadow copy,
// classifying each match as an older version, a deleted file or a deleted folder.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "PrevFileRecord.h"

struct ScanOptions
{
    std::wstring baseFolder;      // BaseFolder
    std::wstring wildcard = L"*"; // FilesWildcard
    int  depth = 0;               // SubfoldersDepth (0 = Unlimited)
    bool findPrevVersions = true; // FindPreviousFiles
    bool findDeletedFiles = true; // FindDeletedFiles
    bool findDeletedFolders = true; // FindDeletedFolders
    int  mode = 2;                // MultipleCopiesMode: 1 all, 2 different, 3 latest, 4 selected
    std::wstring selectedShadow;  // device object path when mode == 4
    DisplayOptions disp;
};

// Progress callback: receives the running item count. Return false to cancel.
using ScanProgress = std::function<bool(int count)>;

std::vector<PrevFileRecord> ScanPreviousFiles(const ScanOptions& opt, const ScanProgress& progress = {});
