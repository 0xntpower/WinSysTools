// ResourceScan.h -- the icon/cursor discovery engine.
// Reconstructs the original's file/wildcard walk (sub_404D64), per-file resource
// enumeration (sub_404C2B), record build + optional size/colour filter
// (sub_404A01 / sub_401610) and process-module scan (sub_404D45).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "IconEntry.h"

struct ScanOptions
{
    bool icons = true;
    bool cursors = true;
    bool subdirs = false;
    bool filterSize = false;
    int  width = 0;
    int  height = 0;
    bool filterColor = false;
    int  colorBits = 0;
};

// Invoked for each accepted entry while its module is still loaded, so the caller
// can pull preview icons into its image lists. May be left empty (CLI path).
using EntryVisitor = std::function<void(HMODULE, IconEntry&)>;

class IconScanner
{
public:
    ScanOptions          options;
    std::vector<IconEntry> results;
    EntryVisitor         visit;
    const volatile bool* cancel = nullptr;   // optional stop flag polled during the walk

    // path may be a single file, a wildcard, or a folder (expanded to "\\*.*").
    void scanPath(const std::wstring& path);
    void scanProcess(DWORD pid);
    // Load and enumerate exactly one module path (the /save CLI path, sub_4060D5).
    void scanFile(const std::wstring& file);

private:
    bool cancelled() const { return cancel && *cancel; }
    void scanWildcard(const std::wstring& wildcard);
    void enumModule(HMODULE hModule, const std::wstring& file);
};

// Extract a preview HICON for one group (sub_401599 / sub_40178A).
[[nodiscard]] HICON ExtractGroupIcon(HMODULE hModule, const wchar_t* name, bool cursor, int cx, int cy);
