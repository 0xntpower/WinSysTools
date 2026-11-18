// HeapEnumerator.h -- walks the heap blocks of a target process.
#pragma once
#include <windows.h>
#include <vector>
#include "HeapBlock.h"

struct HeapScanOptions
{
    bool useFromSize = true; // Filter.UseFromSize, default on
    uint32_t fromSize = 80;  // Filter.FromSize, default 80 bytes
    bool useToSize = false;  // Filter.UseToSize, default off
    uint32_t toSize = 10000; // Filter.ToSize, default 10000
};

// Enumerate every busy heap block of the process that passes the size filter.
// Mirrors the original scan: TH32CS_SNAPHEAPLIST + Heap32ListFirst/Next for the
// heap list, then a direct structure walk per heap through ReadProcessMemory.
std::vector<HeapBlock> EnumerateHeapBlocks(DWORD pid, const HeapScanOptions& opt);

// Read raw bytes of a block's data for the Data Preview column and the lower pane.
// Opens the process, reads nSize bytes at address, closes. Returns bytes actually read.
SIZE_T ReadProcessBytes(DWORD pid, uint64_t address, void* buffer, SIZE_T nSize);
