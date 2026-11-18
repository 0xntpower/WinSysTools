// Coreinfo - source-accurate recreation of the Sysinternals Coreinfo console tool.
//
// Reports the mapping between logical and physical processors, NUMA nodes,
// sockets and caches, plus the CPU feature set, using the CPUID instruction and
// the Windows GetLogicalProcessorInformation/Ex APIs. Reverse engineered from
// Coreinfo64.exe v3.6. The CPUID feature table lives in FeatureTable.h and was
// extracted verbatim from the original binary.
//
// Switches: -c cores, -f features, -g groups, -l caches, -m NUMA access cost,
// -n NUMA nodes, -s sockets, -v virtualization, -d CPU distance, -accepteula,
// -nobanner, and the default which selects everything except -v and -d.

#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "FeatureTable.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "version.lib")

namespace
{
    // Section selection bits, matching the original switch parser exactly.
    constexpr unsigned kFlagCores      = 0x001;
    constexpr unsigned kFlagNuma       = 0x002;
    constexpr unsigned kFlagSockets    = 0x004;
    constexpr unsigned kFlagCaches     = 0x008;
    constexpr unsigned kFlagGroups     = 0x010;
    constexpr unsigned kFlagNumaCost   = 0x020;
    constexpr unsigned kFlagFeatures   = 0x040;
    constexpr unsigned kFlagVirtual    = 0x080;
    constexpr unsigned kFlagDistance   = 0x100;
    constexpr unsigned kFlagDefault    = 0xFFFFFEFFu;   // everything except distance

    // Vendor codes passed to the feature routines.
    constexpr int kVendorAmd   = 0;
    constexpr int kVendorIntel = 1;
    constexpr int kVendorOther = 3;

    constexpr ULONG kSystemCetInformation = 221;   // NtQuerySystemInformation class

    // RAII wrapper over an HKEY so every open key is closed on every path.
    class RegKey
    {
    public:
        RegKey() = default;
        ~RegKey()
        {
            if (m_key != nullptr)
            {
                RegCloseKey(m_key);
            }
        }
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;

        [[nodiscard]] LSTATUS OpenLocalMachine(const wchar_t* subKey)
        {
            return RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &m_key);
        }
        [[nodiscard]] HKEY Get() const { return m_key; }
        [[nodiscard]] HKEY* Put() { return &m_key; }

    private:
        HKEY m_key = nullptr;
    };

    // Four CPUID output registers in EAX, EBX, ECX, EDX order.
    struct CpuidRegs
    {
        int v[4] = { 0, 0, 0, 0 };
    };

    [[nodiscard]] CpuidRegs Cpuid(unsigned leaf)
    {
        CpuidRegs r;
        __cpuid(r.v, static_cast<int>(leaf));
        return r;
    }

    [[nodiscard]] CpuidRegs CpuidEx(unsigned leaf, unsigned subLeaf)
    {
        CpuidRegs r;
        __cpuidex(r.v, static_cast<int>(leaf), static_cast<int>(subLeaf));
        return r;
    }

    [[nodiscard]] bool BitSet(int value, int bit)
    {
        return ((static_cast<unsigned>(value) >> bit) & 1u) != 0;
    }

    // Detect the vendor the same way the original does, from leaf 0 registers.
    [[nodiscard]] int DetectVendor()
    {
        const CpuidRegs r = Cpuid(0);
        if (r.v[1] == 0x756E6547 && r.v[3] == 0x49656E69 && r.v[2] == 0x6C65746E)
        {
            return kVendorIntel;   // GenuineIntel
        }
        if (r.v[1] == 0x68747541 && r.v[3] == 0x69746E65 && r.v[2] == 0x444D4163)
        {
            return kVendorAmd;     // AuthenticAMD
        }
        return kVendorOther;
    }

    // Narrow a registry or version wide string for the narrow output stream,
    // matching how the original runtime converts its wide output.
    [[nodiscard]] std::vector<char> Narrow(const wchar_t* text)
    {
        const int needed = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
        std::vector<char> out(needed > 0 ? static_cast<size_t>(needed) : 1, '\0');
        if (needed > 0)
        {
            WideCharToMultiByte(CP_ACP, 0, text, -1, out.data(), needed, nullptr, nullptr);
        }
        return out;
    }

    // ---- EULA -----------------------------------------------------------------
    // The original stores acceptance in HKCU\Software\Sysinternals\Coreinfo as the
    // REG_DWORD value EulaAccepted. -accepteula sets it and runs headless.

    constexpr const wchar_t* kEulaSubKey = L"Software\\Sysinternals\\Coreinfo";
    constexpr const wchar_t* kEulaValue  = L"EulaAccepted";

    [[nodiscard]] bool EulaAlreadyAccepted()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kEulaSubKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD size = sizeof(data);
        const LSTATUS status = RegQueryValueExW(key, kEulaValue, nullptr, nullptr,
                                                reinterpret_cast<LPBYTE>(&data), &size);
        RegCloseKey(key);
        return status == ERROR_SUCCESS && data != 0;
    }

    void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (RegCreateKeyW(HKEY_CURRENT_USER, kEulaSubKey, &key) == ERROR_SUCCESS)
        {
            DWORD data = 1;
            RegSetValueExW(key, kEulaValue, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&data), sizeof(data));
            RegCloseKey(key);
        }
    }

    // Remove a matching switch from argv in place and report whether it was present.
    [[nodiscard]] bool TakeSwitch(int& argc, wchar_t** argv, const wchar_t* dash, const wchar_t* slash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (_wcsicmp(argv[i], dash) == 0 || _wcsicmp(argv[i], slash) == 0)
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }

    // ---- Banner ---------------------------------------------------------------
    // The original builds the banner from its own version resource. Read the same
    // standard string keys so the line matches "Coreinfo v3.6 - ...".

    [[nodiscard]] const wchar_t* VersionString(const BYTE* block, const DWORD* translation, const wchar_t* key)
    {
        wchar_t subBlock[256];
        swprintf(subBlock, 256, L"\\StringFileInfo\\%04X%04X\\%s",
                 LOWORD(*translation), HIWORD(*translation), key);
        void* value = nullptr;
        UINT len = 0;
        if (VerQueryValueW(block, subBlock, &value, &len) && value != nullptr)
        {
            return static_cast<const wchar_t*>(value);
        }
        return L"";
    }

    void PrintBanner()
    {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
        {
            return;
        }
        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW(path, &handle);
        if (size == 0)
        {
            return;
        }
        std::vector<BYTE> block(size);
        if (!GetFileVersionInfoW(path, 0, size, block.data()))
        {
            return;
        }
        void* trans = nullptr;
        UINT transLen = 0;
        if (!VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", &trans, &transLen) || trans == nullptr)
        {
            return;
        }
        const DWORD* translation = static_cast<const DWORD*>(trans);
        const std::vector<char> name = Narrow(VersionString(block.data(), translation, L"InternalName"));
        const std::vector<char> ver  = Narrow(VersionString(block.data(), translation, L"FileVersion"));
        const std::vector<char> desc = Narrow(VersionString(block.data(), translation, L"FileDescription"));
        const std::vector<char> copy = Narrow(VersionString(block.data(), translation, L"LegalCopyright"));
        const std::vector<char> comp = Narrow(VersionString(block.data(), translation, L"CompanyName"));
        printf("\n%s v%s - %s\n%s\n%s\n\n",
               name.data(), ver.data(), desc.data(), copy.data(), comp.data());
    }

    // ---- CPU header -----------------------------------------------------------
    // Brand, identifier and microcode come from the registry, not CPUID, exactly
    // as the original reads them.

    void PrintCpuHeader()
    {
        printf("\n");

        RegKey cpu;
        if (cpu.OpenLocalMachine(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0") == ERROR_SUCCESS)
        {
            BYTE data[520];
            DWORD size = sizeof(data);
            if (RegQueryValueExW(cpu.Get(), L"ProcessorNameString", nullptr, nullptr, data, &size) == ERROR_SUCCESS)
            {
                const std::vector<char> narrow = Narrow(reinterpret_cast<const wchar_t*>(data));
                printf("%s\n", narrow.data());
            }

            bool haveMicro = false;
            DWORD micro = 0;
            const wchar_t* names[2] = { L"Update Revision", L"Update Signature" };
            for (const wchar_t* name : names)
            {
                if (haveMicro)
                {
                    break;
                }
                BYTE four[4];
                DWORD cb = sizeof(four);
                if (RegQueryValueExW(cpu.Get(), name, nullptr, nullptr, four, &cb) == ERROR_SUCCESS && cb == 4)
                {
                    micro = *reinterpret_cast<DWORD*>(four);
                    haveMicro = true;
                }
                else
                {
                    BYTE eight[8];
                    DWORD cb8 = sizeof(eight);
                    if (RegQueryValueExW(cpu.Get(), name, nullptr, nullptr, eight, &cb8) == ERROR_SUCCESS && cb8 == 8)
                    {
                        micro = *reinterpret_cast<DWORD*>(eight + 4);
                        haveMicro = true;
                    }
                }
            }

            RegKey env;
            if (env.OpenLocalMachine(L"System\\CurrentControlSet\\Control\\Session Manager\\Environment") == ERROR_SUCCESS)
            {
                BYTE idData[520];
                DWORD idSize = sizeof(idData);
                if (RegQueryValueExW(env.Get(), L"processor_identifier", nullptr, nullptr, idData, &idSize) == ERROR_SUCCESS)
                {
                    const std::vector<char> narrow = Narrow(reinterpret_cast<const wchar_t*>(idData));
                    printf("%s\n", narrow.data());
                }
            }

            if (haveMicro)
            {
                printf("Microcode signature: %08X\n", micro);
            }
        }
    }

    // ---- Feature helpers ------------------------------------------------------

    void PrintFeatureLine(const char* name, bool supported, const char* desc)
    {
        printf("%-10s\t", name);
        printf("%s\t%s\n", supported ? "*" : "-", desc);
    }

    // Pick the register value a table entry references from the fetched leaves.
    [[nodiscard]] bool EvaluateEntry(const FeatureEntry& e,
                                     const CpuidRegs& leaf1, const CpuidRegs& leaf7,
                                     const CpuidRegs& leafExt1, const CpuidRegs& leafExt7)
    {
        const CpuidRegs* regs = &leafExt1;
        switch (e.Leaf)
        {
            case 0x00000001: regs = &leaf1;    break;
            case 0x00000007: regs = &leaf7;    break;
            case 0x80000001: regs = &leafExt1; break;
            case 0x80000007: regs = &leafExt7; break;
            default:         regs = &leafExt1; break;
        }
        return BitSet(regs->v[e.Reg], e.Bit);
    }

    // Open-coded NtQuerySystemInformation import for the CET query. The return is
    // an NTSTATUS, declared as LONG to avoid pulling in the native headers.
    using PFN_NtQuerySystemInformation = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

    void PrintCetLines()
    {
        bool cetSupported = false;
        bool kernelCet = false;
        bool userCet = false;
        bool haveResult = false;

        const HMODULE ntdll = GetModuleHandleW(L"ntdll");
        if (ntdll != nullptr)
        {
            const auto query = reinterpret_cast<PFN_NtQuerySystemInformation>(
                GetProcAddress(ntdll, "NtQuerySystemInformation"));
            if (query != nullptr)
            {
                ULONG value = 0;
                if (query(kSystemCetInformation, &value, sizeof(value), nullptr) == 0)
                {
                    cetSupported = (value & 1u) != 0;
                    userCet = ((value >> 1) & 1u) != 0;
                    kernelCet = ((value >> 8) & 1u) != 0;
                    haveResult = true;
                }
            }
        }

        if (!haveResult)
        {
            // Without the kernel query the original falls back to its helper driver,
            // which needs administrative rights. Report that and skip the lines.
            printf("Querying CET support requires admin rights\n");
            return;
        }

        PrintFeatureLine("CET", cetSupported, "Supports Control Flow Enforcement Technology");
        PrintFeatureLine("Kernel CET", kernelCet, "Kernel-mode CET Enabled");
        PrintFeatureLine("User CET", userCet, "User-mode CET Allowed");
    }

    // ---- Feature dump (-f) ----------------------------------------------------

    void DumpFeatures(int vendor)
    {
        const HANDLE thread = GetCurrentThread();
        const DWORD_PTR savedAffinity = SetThreadAffinityMask(thread, 1);

        PrintCpuHeader();

        const CpuidRegs leaf0 = Cpuid(0);
        const unsigned maxBasic = static_cast<unsigned>(leaf0.v[0]);

        CpuidRegs leaf1;
        if (maxBasic >= 1)
        {
            leaf1 = Cpuid(1);
        }
        CpuidRegs leaf7;
        if (maxBasic >= 7)
        {
            leaf7 = CpuidEx(7, 0);
        }

        const CpuidRegs leafExt0 = Cpuid(0x80000000);
        const unsigned maxExt = static_cast<unsigned>(leafExt0.v[0]);
        CpuidRegs leafExt1;
        if (maxExt >= 0x80000001)
        {
            leafExt1 = Cpuid(0x80000001);
        }
        CpuidRegs leafExt7;
        if (maxExt >= 0x80000007)
        {
            leafExt7 = Cpuid(0x80000007);
        }
        unsigned addressWidth = 0;
        if (maxExt >= 0x80000008)
        {
            addressWidth = static_cast<unsigned>(Cpuid(0x80000008).v[0]) & 0xFFFF;
        }

        // HTT is vendor specific. Intel labels it hyperthreading, AMD multicore.
        bool htt = false;
        if (BitSet(leaf1.v[3], 28))
        {
            if (vendor == kVendorIntel)
            {
                htt = (leaf1.v[1] & 0x00FF0000) != 0;
            }
            else if (vendor == kVendorAmd)
            {
                htt = (leafExt1.v[2] & 2) != 0;
            }
        }
        PrintFeatureLine("HTT", htt, vendor == kVendorIntel ? "Hyperthreading enabled" : "Multicore");

        PrintCetLines();

        for (const FeatureEntry& e : kFeatureTable)
        {
            if (e.Bit == kSeparator)
            {
                printf("\n");
                continue;
            }
            const bool supported = EvaluateEntry(e, leaf1, leaf7, leafExt1, leafExt7);
            PrintFeatureLine(e.Name, supported, e.Desc);
        }

        // PREFETCHW support, from leaf 0x80000001 ECX bit 8 (3DNowPrefetch). The
        // original probes the instruction at runtime for the same result.
        printf("\n");
        PrintFeatureLine("PREFETCHW", BitSet(leafExt1.v[2], 8), "Supports PREFETCHW instruction");

        SetThreadAffinityMask(thread, savedAffinity);

        printf("\nMaximum implemented CPUID leaves: %08X (Basic), %08X (Extended).", maxBasic, maxExt);
        printf("\nMaximum implemented address width: %lu bits (virtual), %lu bits (physical).\n",
               (addressWidth >> 8) & 0xFF, addressWidth & 0xFF);
        printf("\nProcessor signature: %08X\n", static_cast<unsigned>(leaf1.v[0]));
    }

    // ---- Virtualization dump (-v) ---------------------------------------------

    bool DumpVirtualization(int vendor)
    {
        PrintCpuHeader();

        const CpuidRegs leaf1 = Cpuid(1);
        CpuidRegs regLeaf1 = leaf1;
        CpuidRegs leaf7 = CpuidEx(7, 0);
        CpuidRegs leafExt1 = Cpuid(0x80000001);
        CpuidRegs leafExt7 = Cpuid(0x80000007);

        if (vendor == kVendorAmd)
        {
            // NP (SLAT) is leaf 0x8000000A EDX bit 0, parked into the leaf 1 EAX slot
            // so the table entry can read it as leaf 1 / EAX / bit 0.
            const CpuidRegs leafExt0 = Cpuid(0x80000000);
            int np = 0;
            if (static_cast<unsigned>(leafExt0.v[0]) >= 0x8000000A)
            {
                np = (Cpuid(0x8000000A).v[3] & 1);
            }
            regLeaf1.v[0] = np;

            for (const FeatureEntry& e : kVirtTableAmd)
            {
                const bool supported = EvaluateEntry(e, regLeaf1, leaf7, leafExt1, leafExt7);
                PrintFeatureLine(e.Name, supported, e.Desc);
            }
            return true;
        }

        if (vendor == kVendorIntel)
        {
            // EPT and URG detection needs the helper driver and administrative rights.
            // Without it the original stops after the header, so do the same.
            return false;
        }

        return false;
    }

    // ---- Topology (GetLogicalProcessorInformationEx) --------------------------

    using InfoEx = SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;

    [[nodiscard]] const InfoEx* NextRecord(const InfoEx* rec)
    {
        return reinterpret_cast<const InfoEx*>(reinterpret_cast<const BYTE*>(rec) + rec->Size);
    }

    // Print one processor affinity map line for every active group. Mirrors the
    // original helper: for each group, pick the matching GROUP_AFFINITY mask or
    // fall back to the whole group, then emit '*' for each set processor bit.
    void PrintAffinityMap(const GROUP_RELATIONSHIP* groups, const GROUP_AFFINITY* masks, WORD count)
    {
        for (WORD g = 0; g < groups->ActiveGroupCount; ++g)
        {
            WORD idx = count;
            for (WORD k = 0; k < count; ++k)
            {
                if (masks[k].Group == g)
                {
                    idx = k;
                    break;
                }
            }
            const BYTE procs = groups->GroupInfo[g].ActiveProcessorCount;
            const KAFFINITY mask = (idx == count)
                ? groups->GroupInfo[g].ActiveProcessorMask
                : masks[idx].Mask;

            char line[65];
            BYTE n = 0;
            for (; n < procs && n < 64; ++n)
            {
                line[n] = (mask & (static_cast<KAFFINITY>(1) << n)) ? '*' : '-';
            }
            line[n] = '\0';
            fputs(line, stdout);
            if (groups->ActiveGroupCount > 1)
            {
                printf("\n");
            }
        }
    }

    [[nodiscard]] const GROUP_RELATIONSHIP* FindGroupRelationship(const BYTE* buffer, DWORD length)
    {
        const InfoEx* rec = reinterpret_cast<const InfoEx*>(buffer);
        const BYTE* end = buffer + length;
        while (reinterpret_cast<const BYTE*>(rec) < end)
        {
            if (rec->Relationship == RelationGroup)
            {
                return &rec->Group;
            }
            rec = NextRecord(rec);
        }
        return &reinterpret_cast<const InfoEx*>(buffer)->Group;
    }

    void DumpCores(const BYTE* buffer, DWORD length, const GROUP_RELATIONSHIP* groups)
    {
        printf("\nLogical to Physical Processor Map:\n");
        unsigned coreNumber = 0;
        const InfoEx* rec = reinterpret_cast<const InfoEx*>(buffer);
        const BYTE* end = buffer + length;
        while (reinterpret_cast<const BYTE*>(rec) < end)
        {
            if (rec->Relationship == RelationProcessorCore)
            {
                const PROCESSOR_RELATIONSHIP& pr = rec->Processor;
                const bool smt = (pr.Flags & LTP_PC_SMT) != 0;
                if (groups->ActiveGroupCount > 1)
                {
                    printf("Physical Processor %d", coreNumber++);
                    if (smt)
                    {
                        printf(" (Hyperthreaded)");
                    }
                    printf(":\n");
                }
                PrintAffinityMap(groups, &pr.GroupMask[0], 1);
                if (groups->ActiveGroupCount == 1)
                {
                    printf("  Physical Processor %d", coreNumber++);
                    if (smt)
                    {
                        printf(" (Hyperthreaded)");
                    }
                    printf("\n");
                }
            }
            rec = NextRecord(rec);
        }
    }

    void DumpSockets(const BYTE* buffer, DWORD length, const GROUP_RELATIONSHIP* groups)
    {
        printf("\nLogical Processor to Socket Map:\n");
        unsigned socketNumber = 0;
        const InfoEx* rec = reinterpret_cast<const InfoEx*>(buffer);
        const BYTE* end = buffer + length;
        while (reinterpret_cast<const BYTE*>(rec) < end)
        {
            if (rec->Relationship == RelationProcessorPackage)
            {
                const PROCESSOR_RELATIONSHIP& pr = rec->Processor;
                if (groups->ActiveGroupCount > 1)
                {
                    printf("Socket %d:\n", socketNumber++);
                }
                PrintAffinityMap(groups, &pr.GroupMask[0], pr.GroupCount);
                if (groups->ActiveGroupCount == 1)
                {
                    printf("  Socket %d\n", socketNumber++);
                }
            }
            rec = NextRecord(rec);
        }
    }

    void DumpNuma(const BYTE* buffer, DWORD length, const GROUP_RELATIONSHIP* groups)
    {
        printf("\nLogical Processor to NUMA Node Map:\n");
        ULONG highest = 0;
        GetNumaHighestNodeNumber(&highest);
        ++highest;

        unsigned seen = 0;
        const InfoEx* rec = reinterpret_cast<const InfoEx*>(buffer);
        const BYTE* end = buffer + length;
        while (reinterpret_cast<const BYTE*>(rec) < end)
        {
            if (rec->Relationship == RelationNumaNode)
            {
                const NUMA_NODE_RELATIONSHIP& nn = rec->NumaNode;
                if (groups->ActiveGroupCount > 1)
                {
                    printf("NUMA Node %d:\n", nn.NodeNumber);
                }
                WORD count = nn.GroupCount;
                if (count == 0)
                {
                    count = 1;
                }
                PrintAffinityMap(groups, &nn.GroupMask, count);
                if (groups->ActiveGroupCount == 1)
                {
                    printf("  NUMA Node %d\n", nn.NodeNumber);
                }
                if (nn.NodeNumber + 1 > seen)
                {
                    seen = nn.NodeNumber + 1;
                }
            }
            rec = NextRecord(rec);
        }

        // Pad trailing nodes that report no processors, as the original does.
        for (; seen < highest; ++seen)
        {
            const WORD active = groups->GroupInfo[0].ActiveProcessorCount;
            char line[65];
            BYTE n = 0;
            for (; n < active && n < 64; ++n)
            {
                line[n] = '-';
            }
            line[n] = '\0';
            fputs(line, stdout);
            if (groups->ActiveGroupCount == 1)
            {
                printf("  NUMA Node %d\n", seen);
            }
        }
    }

    void DumpNumaCost(const BYTE* buffer, DWORD length)
    {
        // Count distinct NUMA nodes. A single node prints the short line. The full
        // cross-node cost matrix needs the helper driver and is not reproduced here.
        unsigned nodes = 0;
        const InfoEx* rec = reinterpret_cast<const InfoEx*>(buffer);
        const BYTE* end = buffer + length;
        while (reinterpret_cast<const BYTE*>(rec) < end)
        {
            if (rec->Relationship == RelationNumaNode)
            {
                ++nodes;
            }
            rec = NextRecord(rec);
        }
        if (nodes <= 1)
        {
            printf("\nNo NUMA nodes.\n");
        }
        else
        {
            // ponytail: single-node path only, driver-backed latency matrix not built
            printf("\nApproximate Cross-NUMA Node Access Cost (relative to fastest):\n");
        }
    }

    void DumpCaches(const BYTE* buffer, DWORD length, const GROUP_RELATIONSHIP* groups)
    {
        static const char* kCacheName[5] = {
            "Unified Cache", "Instruction Cache", "Data Cache", "Trace Cache", "Unknown Cache"
        };
        printf("\nLogical Processor to Cache Map:\n");
        int counter[5] = { 0, 0, 0, 0, 0 };

        const InfoEx* rec = reinterpret_cast<const InfoEx*>(buffer);
        const BYTE* end = buffer + length;
        while (reinterpret_cast<const BYTE*>(rec) < end)
        {
            if (rec->Relationship == RelationCache)
            {
                const CACHE_RELATIONSHIP& c = rec->Cache;
                const unsigned type = (c.Type >= 5) ? 4u : static_cast<unsigned>(c.Type);
                const char* name = (c.Type >= 5) ? "Unknown" : kCacheName[type];
                const int number = counter[type]++;
                const bool megabytes = c.CacheSize >= 0x100000;
                const unsigned sizeValue = megabytes ? (c.CacheSize >> 20) : (c.CacheSize >> 10);
                const char* unit = megabytes ? " MB" : " KB";

                if (groups->ActiveGroupCount > 1)
                {
                    printf("%-17s %3d, Level %d, %4d%s, Assoc %3d, LineSize %3d\n",
                           name, number, c.Level, sizeValue, unit, c.Associativity, c.LineSize);
                }
                PrintAffinityMap(groups, &c.GroupMask, 1);
                if (groups->ActiveGroupCount == 1)
                {
                    printf("  %-17s %3d, Level %d, %4d%s, Assoc %3d, LineSize %3d\n",
                           name, number, c.Level, sizeValue, unit, c.Associativity, c.LineSize);
                }
            }
            rec = NextRecord(rec);
        }
    }

    void DumpGroups(const GROUP_RELATIONSHIP* groups)
    {
        printf("\nLogical Processor to Group Map:\n");
        for (WORD g = 0; g < groups->ActiveGroupCount; ++g)
        {
            if (groups->ActiveGroupCount > 1)
            {
                printf("Group %d:\n", g);
            }
            GROUP_AFFINITY affinity{};
            affinity.Mask = groups->GroupInfo[g].ActiveProcessorMask;
            affinity.Group = g;
            PrintAffinityMap(groups, &affinity, 1);
            if (groups->ActiveGroupCount == 1)
            {
                printf("  Group %d\n", g);
            }
        }
    }

    int RunTopologyAndFeatures(unsigned flags)
    {
        const int vendor = DetectVendor();

        if ((flags & kFlagFeatures) != 0)
        {
            DumpFeatures(vendor);
        }
        else if ((flags & kFlagVirtual) != 0)
        {
            const CpuidRegs leaf1 = Cpuid(1);
            if ((leaf1.v[2] & 0x80000000u) != 0)
            {
                printf("\nNote: Coreinfo must be executed on a system without a hypervisor running for\naccurate results.\n");
            }
            if (!DumpVirtualization(vendor))
            {
                return 1;
            }
        }

        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0)
        {
            printf("Error getting processor information:\n");
            return 1;
        }
        std::vector<BYTE> buffer(length);
        if (!GetLogicalProcessorInformationEx(RelationAll,
                reinterpret_cast<InfoEx*>(buffer.data()), &length))
        {
            printf("Error getting processor information:\n");
            return 1;
        }

        const GROUP_RELATIONSHIP* groups = FindGroupRelationship(buffer.data(), length);

        if ((flags & kFlagCores) != 0)
        {
            DumpCores(buffer.data(), length, groups);
        }
        if ((flags & kFlagSockets) != 0)
        {
            DumpSockets(buffer.data(), length, groups);
        }
        if ((flags & kFlagNuma) != 0)
        {
            DumpNuma(buffer.data(), length, groups);
        }
        if ((flags & kFlagNumaCost) != 0)
        {
            DumpNumaCost(buffer.data(), length);
        }
        if ((flags & kFlagCaches) != 0)
        {
            DumpCaches(buffer.data(), length, groups);
        }
        if ((flags & kFlagGroups) != 0)
        {
            DumpGroups(groups);
        }
        // -d distance needs the helper driver, which is not bundled here.
        return 0;
    }

    void PrintUsage()
    {
        printf("\nFor each resource it shows a map of the OS-visible processors\n");
        printf("that correspond to the specified resources, with '*' representing the \n");
        printf("applicable processors. For example, on a 4-core system, a line in the\n");
        printf("cache output with a map of ");
        printf("shared by cores 3 and 4.\n\n");
        printf("Usage: coreinfo [-c][-f][-g][-l][-n][-s][-m][-v]\n");
        printf("  -c       Dump information on cores.\n");
        printf("  -d       Dump CPU-to-CPU distance.\n");
        printf("  -f       Dump core feature information.\n");
        printf("  -g       Dump information on groups.\n");
        printf("  -l       Dump information on caches.\n");
        printf("  -n       Dump information on NUMA nodes.\n");
        printf("  -s       Dump information on sockets.\n");
        printf("  -m       Dump NUMA access cost.\n");
        printf("  -v       Dump only virtualization-related features including\n");
        printf("           support for second level address translation\n");
        printf("           (requires administrative rights on Intel systems).\n");
        printf("  -nobanner\n");
        printf("           Do not display the startup banner and copyright message.\n");
        printf("\nAll options, except -v and -d, are selected by default.\n");
        printf("\n\n");
    }

    // Parse the resource switches into the selection mask. Returns false if usage
    // was shown for an unknown option.
    [[nodiscard]] bool ParseFlags(int argc, wchar_t** argv, unsigned& flags)
    {
        flags = 0;
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* arg = argv[i];
            if ((arg[0] != L'-' && arg[0] != L'/') || arg[1] == L'\0')
            {
                continue;
            }
            if (_wcsicmp(arg + 1, L"RUN32") == 0 || _wcsicmp(arg + 1, L"ACCEPTEULA") == 0)
            {
                continue;
            }
            for (int j = 1; arg[j] != L'\0'; ++j)
            {
                switch (towupper(arg[j]))
                {
                    case L'C': flags |= kFlagCores;    break;
                    case L'N': flags |= kFlagNuma;     break;
                    case L'S': flags |= kFlagSockets;  break;
                    case L'L': flags |= kFlagCaches;   break;
                    case L'G': flags |= kFlagGroups;   break;
                    case L'M': flags |= kFlagNumaCost; break;
                    case L'F': flags |= kFlagFeatures; break;
                    case L'V': flags |= kFlagVirtual;  break;
                    case L'D': flags |= kFlagDistance; break;
                    default:
                        PrintUsage();
                        return false;
                }
            }
        }
        if (flags == 0)
        {
            flags = kFlagDefault;
        }
        return true;
    }
}

int wmain(int argc, wchar_t** argv)
{
    bool nobanner = TakeSwitch(argc, argv, L"-nobanner", L"/nobanner");
    const bool accept = TakeSwitch(argc, argv, L"-accepteula", L"/accepteula");

    if (!nobanner)
    {
        PrintBanner();
    }

    // EULA gate. A stored flag or -accepteula runs headless.
    if (!EulaAlreadyAccepted())
    {
        if (!accept)
        {
            fprintf(stderr, "This tool requires the Sysinternals license to be accepted. Pass -accepteula.\n\n");
            return 1;
        }
    }
    if (accept)
    {
        WriteEulaAccepted();
    }

    unsigned flags = 0;
    if (!ParseFlags(argc, argv, flags))
    {
        return 0;
    }

    const HMODULE kernel = GetModuleHandleW(L"Kernel32.dll");
    if (kernel == nullptr ||
        GetProcAddress(kernel, "GetLogicalProcessorInformationEx") == nullptr)
    {
        printf("\nCoreinfo requires Windows XP 64-bit or higher, or Windows Server 2003 or higher.\n\n");
        return 2;
    }

    return RunTopologyAndFeatures(flags);
}
