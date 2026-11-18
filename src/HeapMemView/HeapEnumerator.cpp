// HeapEnumerator.cpp -- the heap-block walk, recovered from sub_140002074 (heap
// list), sub_140004C68 (NT6+ walker), sub_140004E00 (pre-Vista walker) and the
// collector filter sub_140001F64.
//
// The original picks the walker by OS major version. Windows XP and Server 2003
// (major <= 5) use the pre-Vista layout, everything from Vista on uses the NT6+
// layout. Both read the live process heap structures directly with
// ReadProcessMemory rather than calling Heap32First/Heap32Next, because those
// return only the busy blocks and the tool wants the exact header addresses and
// the encoded sizes.
//
// Every x64 heap block has a 16-byte _HEAP_ENTRY header. On NT6+ the first eight
// bytes are unused and the Size word and Flags byte sit at entry offsets 8 and 10,
// XOR-encoded with the heap's Encoding key. The size is counted in 16-byte units.
#include "HeapEnumerator.h"
#include <tlhelp32.h>

namespace
{
    constexpr DWORD kNtHeapSignature = 0xFFEEFFEE; // _HEAP / _HEAP_SEGMENT signature
    constexpr int kMaxSegments = 64;

    // Read a typed field out of a local buffer at a byte offset.
    template <class T> T at(const BYTE* p, size_t off)
    {
        T v;
        memcpy(&v, p + off, sizeof(T));
        return v;
    }

    bool PassesFilter(const HeapBlock& b, const HeapScanOptions& opt)
    {
        if ((b.flags & 1) == 0) // only busy (allocated) blocks
        {
            return false;
        }
        uint32_t data = b.blockSize(); // size shown to the user, header removed
        if (opt.useFromSize && data < opt.fromSize)
        {
            return false;
        }
        if (opt.useToSize && data > opt.toSize)
        {
            return false;
        }
        return true;
    }

    void AddBlock(std::vector<HeapBlock>& out, const HeapScanOptions& opt, HeapBlock b)
    {
        if (b.rawSize >= kHeapEntrySize && PassesFilter(b, opt))
        {
            out.push_back(b);
        }
    }

    // Vista and later. segPtr starts at the heap base, which is also the first
    // _HEAP_SEGMENT, and the segment list is followed through the link at +0x18.
    void WalkNT6(HANDLE proc, uint64_t heapBase, const HeapScanOptions& opt, std::vector<HeapBlock>& out)
    {
        uint64_t segPtr = heapBase;
        for (int seg = 0; seg < kMaxSegments; ++seg)
        {
            BYTE buf[0x1B0];
            SIZE_T got = 0;
            if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(segPtr), buf, sizeof(buf), &got) || got == 0)
            {
                break;
            }

            if (at<DWORD>(buf, 0x10) == kNtHeapSignature)
            {
                uint64_t entry = at<uint64_t>(buf, 0x40);     // FirstEntry
                uint64_t lastValid = at<uint64_t>(buf, 0x48); // LastValidEntry
                uint16_t encSize = at<uint16_t>(buf, 0x88);   // Encoding.Size key
                BYTE encFlags = buf[0x8A];                    // Encoding.Flags key

                while (entry < lastValid)
                {
                    BYTE ent[0x10];
                    SIZE_T n = 0;
                    if (ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(entry), ent, sizeof(ent), &n) && n != 0)
                    {
                        uint16_t sizeWord = at<uint16_t>(ent, 8);
                        uint32_t blockSize = 16u * static_cast<uint16_t>(sizeWord ^ encSize);
                        if (blockSize == 0)
                        {
                            break;
                        }
                        HeapBlock b;
                        b.heap = heapBase;
                        b.header = entry;
                        b.rawSize = blockSize;
                        b.flags = static_cast<BYTE>(ent[0x0A] ^ encFlags);
                        b.segIndex = seg;
                        AddBlock(out, opt, b);
                        entry += blockSize;
                    }
                    else
                    {
                        entry += 0x1000; // skip an unreadable page
                    }
                }
            }

            uint64_t flink = at<uint64_t>(buf, 0x18); // next segment list link
            if ((flink & 0xFFF) == 0x10)
            {
                segPtr = flink - 0x10;
            }
            else
            {
                break;
            }
        }
    }

    // Pre-Vista layout (Windows 2000 / XP / Server 2003). The _HEAP holds a 64-slot
    // Segments array at offset 0x80, each entry points at a _HEAP_SEGMENT, and the
    // entry header is not encoded. This path does not run on the validation OS and
    // is kept for fidelity with the original's version split.
    void WalkNT5(HANDLE proc, uint64_t heapBase, const HeapScanOptions& opt, std::vector<HeapBlock>& out)
    {
        BYTE heap[0xAC8];
        SIZE_T got = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(heapBase), heap, sizeof(heap), &got) || got == 0)
        {
            return;
        }

        for (int seg = 0; seg < kMaxSegments; ++seg)
        {
            uint64_t segPtr = at<uint64_t>(heap, 0x80 + static_cast<size_t>(seg) * 8);
            if (segPtr == 0)
            {
                continue;
            }

            BYTE sbuf[0x68];
            SIZE_T sn = 0;
            if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(segPtr), sbuf, sizeof(sbuf), &sn) || sn == 0)
            {
                continue;
            }
            if (at<DWORD>(sbuf, 0x10) != kNtHeapSignature)
            {
                continue;
            }

            uint64_t entry = at<uint64_t>(sbuf, 0x38);     // FirstEntry
            uint64_t lastValid = at<uint64_t>(sbuf, 0x60); // LastValidEntry
            while (entry <= lastValid)
            {
                BYTE ent[0x10];
                SIZE_T n = 0;
                if (ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(entry), ent, sizeof(ent), &n) && n != 0)
                {
                    uint32_t blockSize = 16u * at<uint16_t>(ent, 0);
                    if (blockSize == 0)
                    {
                        break;
                    }
                    HeapBlock b;
                    b.heap = heapBase;
                    b.header = entry;
                    b.rawSize = blockSize;
                    b.flags = ent[9];
                    b.segIndex = seg;
                    AddBlock(out, opt, b);
                    entry += blockSize;
                }
                else
                {
                    entry += 0x1000;
                }
            }
        }
    }

    bool PreVistaKernel()
    {
        OSVERSIONINFOW vi{sizeof(vi)};
#pragma warning(push)
#pragma warning(disable : 4996)
        GetVersionExW(&vi);
#pragma warning(pop)
        return vi.dwPlatformId == VER_PLATFORM_WIN32_NT && vi.dwMajorVersion <= 5;
    }
} // namespace

std::vector<HeapBlock> EnumerateHeapBlocks(DWORD pid, const HeapScanOptions& opt)
{
    std::vector<HeapBlock> out;

    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc)
    {
        proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }
    if (!proc)
    {
        return out;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPHEAPLIST, pid);
    if (snap != INVALID_HANDLE_VALUE)
    {
        const bool legacy = PreVistaKernel();
        HEAPLIST32 hl{sizeof(hl)};
        if (Heap32ListFirst(snap, &hl))
        {
            do
            {
                if (legacy)
                {
                    WalkNT5(proc, hl.th32HeapID, opt, out);
                }
                else
                {
                    WalkNT6(proc, hl.th32HeapID, opt, out);
                }
            } while (Heap32ListNext(snap, &hl));
        }
        CloseHandle(snap);
    }

    CloseHandle(proc);
    return out;
}

SIZE_T ReadProcessBytes(DWORD pid, uint64_t address, void* buffer, SIZE_T nSize)
{
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc)
    {
        proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }
    if (!proc)
    {
        return 0;
    }
    SIZE_T got = 0;
    ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(address), buffer, nSize, &got);
    CloseHandle(proc);
    return got;
}
