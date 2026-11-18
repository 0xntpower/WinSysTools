// HeapBlock.h -- one row in the upper pane: a single allocated heap block.
//
// The original stores five raw fields per block plus a "new" marker, and renders
// each column from them in the getter sub_140001E24. The field-to-column mapping,
// recovered exactly from that getter, is:
//
//   col 0  Data Address    header + 16   (0x%016llx)   data returned by HeapAlloc
//   col 1  Header Address  header        (0x%016llx)   the _HEAP_ENTRY address
//   col 2  Block Size      rawSize - 16  (decimal)     data bytes, header removed
//   col 3  Heap Handle     heap          (0x%016llx)   th32HeapID / heap base
//   col 4  Segment Index   segIndex      (decimal)
//   col 5  New             "*" when isNew
//   col 6  Data Preview    first N data bytes as ASCII or hex (filled by the frame)
//
// Every x64 heap block carries a 16-byte _HEAP_ENTRY header, so the data address is
// the header address plus 16 and the usable block size is the raw size minus 16.
#pragma once
#include <windows.h>
#include <string>
#include <cstdio>

inline constexpr DWORD kHeapEntrySize = 16; // x64 _HEAP_ENTRY header length

struct HeapBlock
{
    uint64_t heap = 0;    // th32HeapID (heap base / "Heap Handle")
    uint64_t header = 0;  // address of the block's _HEAP_ENTRY
    uint32_t rawSize = 0; // 16 * (encodedSize ^ encodingKey), includes the header
    uint32_t flags = 0;   // decoded _HEAP_ENTRY flags byte (bit 0 = busy)
    int32_t segIndex = 0; // segment ordinal within the heap
    bool isNew = false;
    bool previewDone = false; // Data Preview filled in yet
    std::wstring preview;     // Data Preview text, computed on demand

    uint64_t dataAddress() const
    {
        return header + kHeapEntrySize;
    }
    uint32_t blockSize() const
    {
        return rawSize - kHeapEntrySize;
    }

    static std::wstring hex64(uint64_t v)
    {
        wchar_t b[24];
        swprintf(b, 24, L"0x%016llx", static_cast<unsigned long long>(v));
        return b;
    }

    std::wstring column(int index) const
    {
        wchar_t num[24];
        switch (index)
        {
        case 0:
            return hex64(dataAddress());
        case 1:
            return hex64(header);
        case 2:
            swprintf(num, 24, L"%u", blockSize());
            return num;
        case 3:
            return hex64(heap);
        case 4:
            swprintf(num, 24, L"%d", segIndex);
            return num;
        case 5:
            return isNew ? L"*" : L"";
        case 6:
            return preview;
        default:
            return L"";
        }
    }

    // Sort key matching the original compare sub_140001DCC: address columns sort by
    // the header address, Block Size by the raw size, Segment Index by the ordinal.
    // Returns negative, zero or positive like wcscmp.
    int compare(const HeapBlock& o, int col) const
    {
        switch (col)
        {
        case 2:
            return (rawSize > o.rawSize) - (rawSize < o.rawSize);
        case 4:
            return (segIndex > o.segIndex) - (segIndex < o.segIndex);
        case 5:
            return (isNew ? 1 : 0) - (o.isNew ? 1 : 0);
        case 6:
            return _wcsicmp(preview.c_str(), o.preview.c_str());
        default:
            return (header > o.header) - (header < o.header);
        }
    }
};
