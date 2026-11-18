// NtApi.h -- the native-API surface the data engine needs from ntdll.
//
// Process Explorer gathers everything in one shot through
// NtQuerySystemInformation, so this declares the matching structure layouts
// and lazily binds the three Nt* entry points by name. The structures follow
// the documented SYSTEM_PROCESS_INFORMATION / SYSTEM_THREAD_INFORMATION and
// the extended handle table. Only the fields the tool reads are named, the
// rest keep their real width so offsets stay correct.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>

namespace ntx
{
    using NTSTATUS = LONG;

    inline constexpr NTSTATUS kStatusSuccess = 0;
    inline constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);

    inline constexpr ULONG kSystemProcessInformation = 5;
    inline constexpr ULONG kSystemExtendedHandleInformation = 64;

    inline constexpr ULONG kObjectNameInformation = 1;
    inline constexpr ULONG kObjectTypeInformation = 2;

    inline constexpr ULONG kProcessBasicInformation = 0;
    inline constexpr ULONG kProcessCommandLineInformation = 60;

    struct ClientId
    {
        HANDLE UniqueProcess;
        HANDLE UniqueThread;
    };

    struct UnicodeString
    {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR  Buffer;
    };

    struct SystemThreadInformation
    {
        LARGE_INTEGER KernelTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER CreateTime;
        ULONG         WaitTime;
        PVOID         StartAddress;
        ClientId      Cid;
        LONG          Priority;
        LONG          BasePriority;
        ULONG         ContextSwitches;
        ULONG         ThreadState;
        ULONG         WaitReason;
    };

    // Full SYSTEM_PROCESS_INFORMATION for the modern kernel. A
    // SystemThreadInformation array of NumberOfThreads follows each record,
    // and NextEntryOffset chains to the next.
    struct SystemProcessInformation
    {
        ULONG         NextEntryOffset;
        ULONG         NumberOfThreads;
        LARGE_INTEGER WorkingSetPrivateSize;
        ULONG         HardFaultCount;
        ULONG         NumberOfThreadsHighWatermark;
        ULONGLONG     CycleTime;
        LARGE_INTEGER CreateTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER KernelTime;
        UnicodeString ImageName;
        LONG          BasePriority;
        HANDLE        UniqueProcessId;
        HANDLE        InheritedFromUniqueProcessId;
        ULONG         HandleCount;
        ULONG         SessionId;
        ULONG_PTR     UniqueProcessKey;
        SIZE_T        PeakVirtualSize;
        SIZE_T        VirtualSize;
        ULONG         PageFaultCount;
        SIZE_T        PeakWorkingSetSize;
        SIZE_T        WorkingSetSize;
        SIZE_T        QuotaPeakPagedPoolUsage;
        SIZE_T        QuotaPagedPoolUsage;
        SIZE_T        QuotaPeakNonPagedPoolUsage;
        SIZE_T        QuotaNonPagedPoolUsage;
        SIZE_T        PagefileUsage;
        SIZE_T        PeakPagefileUsage;
        SIZE_T        PrivatePageCount;
        LARGE_INTEGER ReadOperationCount;
        LARGE_INTEGER WriteOperationCount;
        LARGE_INTEGER OtherOperationCount;
        LARGE_INTEGER ReadTransferCount;
        LARGE_INTEGER WriteTransferCount;
        LARGE_INTEGER OtherTransferCount;
    };

    struct SystemHandleTableEntryInfoEx
    {
        PVOID     Object;
        ULONG_PTR UniqueProcessId;
        ULONG_PTR HandleValue;
        ULONG     GrantedAccess;
        USHORT    CreatorBackTraceIndex;
        USHORT    ObjectTypeIndex;
        ULONG     HandleAttributes;
        ULONG     Reserved;
    };

    struct SystemHandleInformationEx
    {
        ULONG_PTR NumberOfHandles;
        ULONG_PTR Reserved;
        SystemHandleTableEntryInfoEx Handles[1];
    };

    struct ObjectNameInformation
    {
        UnicodeString Name;
    };

    struct ObjectTypeInformation
    {
        UnicodeString TypeName;
        ULONG         Reserved[22];
    };

    struct ProcessBasicInformationBlock
    {
        NTSTATUS  ExitStatus;
        PVOID     PebBaseAddress;
        ULONG_PTR AffinityMask;
        LONG      BasePriority;
        ULONG_PTR UniqueProcessId;
        ULONG_PTR InheritedFromUniqueProcessId;
    };

    using NtQuerySystemInformationFn =
        NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    using NtQueryObjectFn =
        NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    using NtQueryInformationProcessFn =
        NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

    // Lazily resolved ntdll entry points, bound once on first use.
    struct Api
    {
        NtQuerySystemInformationFn    QuerySystemInformation = nullptr;
        NtQueryObjectFn               QueryObject = nullptr;
        NtQueryInformationProcessFn   QueryInformationProcess = nullptr;
    };

    [[nodiscard]] inline const Api& Nt()
    {
        static const Api api = []() noexcept
        {
            Api a{};
            if (HMODULE nt = ::GetModuleHandleW(L"ntdll.dll"))
            {
                a.QuerySystemInformation = reinterpret_cast<NtQuerySystemInformationFn>(
                    reinterpret_cast<void*>(::GetProcAddress(nt, "NtQuerySystemInformation")));
                a.QueryObject = reinterpret_cast<NtQueryObjectFn>(
                    reinterpret_cast<void*>(::GetProcAddress(nt, "NtQueryObject")));
                a.QueryInformationProcess = reinterpret_cast<NtQueryInformationProcessFn>(
                    reinterpret_cast<void*>(::GetProcAddress(nt, "NtQueryInformationProcess")));
            }
            return a;
        }();
        return api;
    }

    // Query a variably sized system-information class into a byte buffer,
    // growing until the call stops reporting a length mismatch.
    [[nodiscard]] inline std::vector<BYTE> QuerySystem(ULONG infoClass, ULONG firstGuess = 0x10000)
    {
        const Api& api = Nt();
        std::vector<BYTE> buf;
        if (!api.QuerySystemInformation)
        {
            return buf;
        }
        ULONG size = firstGuess;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            buf.resize(size);
            ULONG needed = 0;
            const NTSTATUS st = api.QuerySystemInformation(
                infoClass, buf.data(), size, &needed);
            if (st == kStatusSuccess)
            {
                buf.resize(needed ? needed : size);
                return buf;
            }
            if (st == kStatusInfoLengthMismatch)
            {
                size = needed ? (needed + 0x2000) : (size * 2);
                continue;
            }
            buf.clear();
            return buf;
        }
        buf.clear();
        return buf;
    }
}
