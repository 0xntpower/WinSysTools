#pragma once

// CPUID feature table extracted verbatim from Coreinfo64.exe (Sysinternals v3.6).
// Each record maps a CPUID leaf, register (0=EAX 1=EBX 2=ECX 3=EDX) and bit to the
// name and the line Coreinfo prints. A SEP entry emits a blank separator line.

struct FeatureEntry
{
    unsigned int Leaf;
    int          Reg;
    int          Bit;
    const char*  Name;
    const char*  Desc;
};

inline constexpr int kSeparator = -1;
#define SEP { 0u, 0, kSeparator, "", "" }

inline constexpr FeatureEntry kFeatureTable[] = {
    { 0x00000001, 2, 31, "HYPERVISOR", "Hypervisor is present" },
    { 0x00000001, 2,  5, "VMX", "Supports Intel hardware-assisted virtualization" },
    { 0x80000001, 2,  2, "SVM", "Supports AMD hardware-assisted virtualization" },
    { 0x80000001, 3, 29, "X64", "Supports 64-bit mode" },
    SEP,
    { 0x00000001, 2,  6, "SMX", "Supports Intel trusted execution" },
    { 0x80000001, 2, 12, "SKINIT", "Supports AMD SKINIT" },
    { 0x00000007, 1,  2, "SGX", "Supports Intel SGX" },
    SEP,
    { 0x80000001, 3, 20, "NX", "Supports no-execute page protection" },
    { 0x00000007, 1,  7, "SMEP", "Supports Supervisor Mode Execution Prevention" },
    { 0x00000007, 1, 20, "SMAP", "Supports Supervisor Mode Access Prevention" },
    { 0x80000001, 3, 26, "PAGE1GB", "Supports 1 GB large pages" },
    { 0x00000001, 3,  6, "PAE", "Supports > 32-bit physical addresses" },
    { 0x00000001, 3, 16, "PAT", "Supports Page Attribute Table" },
    { 0x00000001, 3,  3, "PSE", "Supports 4 MB pages" },
    { 0x00000001, 3, 17, "PSE36", "Supports > 32-bit address 4 MB pages" },
    { 0x00000001, 3, 13, "PGE", "Supports global bit in page tables" },
    { 0x00000001, 3, 27, "SS", "Supports bus snooping for cache operations" },
    { 0x00000001, 3,  1, "VME", "Supports Virtual-8086 mode" },
    { 0x00000007, 1,  0, "RDWRFSGSBASE", "Supports direct GS/FS base access" },
    SEP,
    { 0x00000001, 3,  0, "FPU", "Implements i387 floating point instructions" },
    { 0x00000001, 3, 23, "MMX", "Supports MMX instruction set" },
    { 0x80000001, 3, 22, "MMXEXT", "Implements AMD MMX extensions" },
    { 0x80000001, 3, 31, "3DNOW", "Supports 3DNow! instructions" },
    { 0x80000001, 3, 30, "3DNOWEXT", "Supports 3DNow! extension instructions" },
    { 0x00000001, 3, 25, "SSE", "Supports Streaming SIMD Extensions" },
    { 0x00000001, 3, 26, "SSE2", "Supports Streaming SIMD Extensions 2" },
    { 0x00000001, 2,  0, "SSE3", "Supports Streaming SIMD Extensions 3" },
    { 0x00000001, 2,  9, "SSSE3", "Supports Supplemental SIMD Extensions 3" },
    { 0x80000001, 2,  4, "SSE4a", "Supports Streaming SIMDR Extensions 4a" },
    { 0x00000001, 2, 19, "SSE4.1", "Supports Streaming SIMD Extensions 4.1" },
    { 0x00000001, 2, 20, "SSE4.2", "Supports Streaming SIMD Extensions 4.2" },
    SEP,
    { 0x00000001, 2, 25, "AES", "Supports AES extensions" },
    { 0x00000001, 2, 28, "AVX", "Supports AVX instruction extensions" },
    { 0x00000007, 1,  5, "AVX2", "Supports AVX2 instruction extensions" },
    { 0x00000007, 1, 16, "AVX-512-F", "Supports AVX-512 Foundation instructions" },
    { 0x00000007, 1, 16, "AVX-512-DQ", "Supports AVX-512 double and quadword instructions" },
    { 0x00000007, 1, 21, "AVX-512-IFAMA", "Supports AVX-512 integer Fused multiply-add instructions" },
    { 0x00000007, 1, 26, "AVX-512-PF", "Supports AVX-512 prefetch instructions" },
    { 0x00000007, 1, 27, "AVX-512-ER", "Supports AVX-512 exponential and reciprocal instructions" },
    { 0x00000007, 1, 28, "AVX-512-CD", "Supports AVX-512 conflict detection instructions" },
    { 0x00000007, 1, 30, "AVX-512-BW", "Supports AVX-512 byte and word instructions" },
    { 0x00000007, 1, 31, "AVX-512-VL", "Supports AVX-512 vector length instructions" },
    { 0x00000001, 2, 12, "FMA", "Supports FMA extensions using YMM state" },
    { 0x00000001, 3,  5, "MSR", "Implements RDMSR/WRMSR instructions" },
    { 0x00000001, 3, 12, "MTRR", "Supports Memory Type Range Registers" },
    { 0x00000001, 2, 26, "XSAVE", "Supports XSAVE/XRSTOR instructions" },
    { 0x00000001, 2, 27, "OSXSAVE", "Supports XSETBV/XGETBV instructions" },
    { 0x00000001, 2, 30, "RDRAND", "Supports RDRAND instruction" },
    { 0x00000007, 1, 18, "RDSEED", "Supports RDSEED instruction" },
    SEP,
    { 0x00000001, 3, 15, "CMOV", "Supports CMOVcc instruction" },
    { 0x00000001, 3, 19, "CLFSH", "Supports CLFLUSH instruction" },
    { 0x00000001, 3,  8, "CX8", "Supports compare and exchange 8-byte instructions" },
    { 0x00000001, 2, 13, "CX16", "Supports CMPXCHG16B instruction" },
    { 0x00000007, 1,  3, "BMI1", "Supports bit manipulation extensions 1" },
    { 0x00000007, 1,  8, "BMI2", "Supports bit manipulation extensions 2" },
    { 0x00000007, 1, 19, "ADX", "Supports ADCX/ADOX instructions" },
    { 0x00000001, 2, 18, "DCA", "Supports prefetch from memory-mapped device" },
    { 0x00000001, 2, 29, "F16C", "Supports half-precision instruction" },
    { 0x00000001, 3, 24, "FXSR", "Supports FXSAVE/FXSTOR instructions" },
    { 0x80000001, 3, 25, "FFXSR", "Supports optimized FXSAVE/FSRSTOR instruction" },
    { 0x00000001, 2,  3, "MONITOR", "Supports MONITOR and MWAIT instructions" },
    { 0x00000001, 2, 22, "MOVBE", "Supports MOVBE instruction" },
    { 0x00000007, 1,  9, "ERMSB", "Supports Enhanced REP MOVSB/STOSB" },
    { 0x00000001, 2,  1, "PCLMULDQ", "Supports PCLMULDQ instruction" },
    { 0x00000001, 2, 23, "POPCNT", "Supports POPCNT instruction" },
    { 0x80000001, 2,  5, "LZCNT", "Supports LZCNT instruction" },
    { 0x00000001, 3, 11, "SEP", "Supports fast system call instructions" },
    { 0x80000001, 2,  0, "LAHF-SAHF", "Supports LAHF/SAHF instructions in 64-bit mode" },
    { 0x00000007, 1,  4, "HLE", "Supports Hardware Lock Elision instructions" },
    { 0x00000007, 1, 11, "RTM", "Supports Restricted Transactional Memory instructions" },
    SEP,
    { 0x00000001, 3,  2, "DE", "Supports I/O breakpoints including CR4.DE" },
    { 0x00000001, 2,  2, "DTES64", "Can write history of 64-bit branch addresses" },
    { 0x00000001, 3, 21, "DS", "Implements memory-resident debug buffer" },
    { 0x00000001, 2,  4, "DS-CPL", "Supports Debug Store feature with CPL" },
    { 0x00000001, 2, 17, "PCID", "Supports PCIDs and settable CR4.PCIDE" },
    { 0x00000007, 1, 10, "INVPCID", "Supports INVPCID instruction" },
    { 0x00000001, 2, 15, "PDCM", "Supports Performance Capabilities MSR" },
    { 0x80000001, 3, 27, "RDTSCP", "Supports RDTSCP instruction" },
    { 0x00000001, 3,  4, "TSC", "Supports RDTSC instruction" },
    { 0x00000001, 2, 24, "TSC-DEADLINE", "Local APIC supports one-shot deadline timer" },
    { 0x80000007, 3,  8, "TSC-INVARIANT", "TSC runs at constant rate" },
    { 0x00000001, 2, 14, "xTPR", "Supports disabling task priority messages" },
    SEP,
    { 0x00000001, 2,  7, "EIST", "Supports Enhanced Intel Speedstep" },
    { 0x00000001, 3, 22, "ACPI", "Implements MSR for power management" },
    { 0x00000001, 3, 29, "TM", "Implements thermal monitor circuitry" },
    { 0x00000001, 2,  8, "TM2", "Implements Thermal Monitor 2 control" },
    { 0x00000001, 3,  9, "APIC", "Implements software-accessible local APIC" },
    { 0x00000001, 2, 21, "x2APIC", "Supports x2APIC" },
    SEP,
    { 0x00000001, 2, 10, "CNXT-ID", "L1 data cache mode adaptive or BIOS" },
    SEP,
    { 0x00000001, 3,  7, "MCE", "Supports Machine Check, INT18 and CR4.MCE" },
    { 0x00000001, 3, 14, "MCA", "Implements Machine Check Architecture" },
    { 0x00000001, 3, 31, "PBE", "Supports use of FERR#/PBE# pin" },
    SEP,
    { 0x00000001, 3, 18, "PSN", "Implements 96-bit processor serial number" },
};

// Virtualization-only table used by the -v switch on AMD parts.
inline constexpr FeatureEntry kVirtTableAmd[] = {
    { 0x00000001, 2, 31, "HYPERVISOR", "Hypervisor is present" },
    { 0x80000001, 2,  2, "SVM", "Supports AMD hardware-assisted virtualization" },
    { 0x00000001, 0,  0, "NP", "Supports AMD nested page tables (SLAT)" },
};

// Virtualization-only table used by the -v switch on Intel parts.
inline constexpr FeatureEntry kVirtTableIntel[] = {
    { 0x00000001, 2, 31, "HYPERVISOR", "Hypervisor is present" },
    { 0x00000001, 2,  5, "VMX", "Supports Intel hardware-assisted virtualization" },
    { 0x00000001, 0,  0, "EPT", "Supports Intel extended page tables (SLAT)" },
    { 0x00000001, 0,  1, "URG", "Supports Intel unrestricted guest" },
};

#undef SEP
