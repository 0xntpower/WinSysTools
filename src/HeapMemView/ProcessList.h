// ProcessList.h -- process enumeration for the Select Process dialog (template 109).
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ProcessInfo
{
    DWORD        pid = 0;
    std::wstring name;    // image base name, e.g. explorer.exe
    std::wstring path;    // full image path when it can be read
};

// Enumerate running processes. The original resolves the full path through psapi
// (EnumProcesses + GetModuleFileNameEx) with a Toolhelp fallback. The recreation
// uses the Toolhelp snapshot for the id and name and QueryFullProcessImageName for
// the path, which needs no psapi import and reads 64-bit paths correctly.
std::vector<ProcessInfo> EnumerateProcesses();
