// PsList process and thread enumeration: the NtQuerySystemInformation layout and
// the one-shot snapshot collection. See analysis/PsList.md for the data-source notes.
#pragma once

#include "../common/PsTools.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pslist
{
    using NtStatus = LONG;
    inline constexpr NtStatus kStatusInfoLengthMismatch = static_cast<NtStatus>(0xC0000004);
    inline constexpr ULONG kSystemProcessInformation = 5;

    struct UnicodeStr
    {
        USHORT Length = 0;
        USHORT MaximumLength = 0;
        PWSTR Buffer = nullptr;
    };

    struct ClientId
    {
        HANDLE UniqueProcess = nullptr;
        HANDLE UniqueThread = nullptr;
    };

    struct SystemThreadInfo
    {
        LARGE_INTEGER KernelTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER CreateTime;
        ULONG WaitTime;
        PVOID StartAddress;
        ClientId Cid;
        LONG Priority;
        LONG BasePriority;
        ULONG ContextSwitches;
        ULONG ThreadState;
        ULONG WaitReason;
    };

    struct SystemProcessInfo
    {
        ULONG NextEntryOffset;
        ULONG NumberOfThreads;
        LARGE_INTEGER WorkingSetPrivateSize;
        ULONG HardFaultCount;
        ULONG NumberOfThreadsHighWatermark;
        ULONGLONG CycleTime;
        LARGE_INTEGER CreateTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER KernelTime;
        UnicodeStr ImageName;
        LONG BasePriority;
        HANDLE UniqueProcessId;
        HANDLE InheritedFromUniqueProcessId;
        ULONG HandleCount;
        ULONG SessionId;
        ULONG_PTR UniqueProcessKey;
        SIZE_T PeakVirtualSize;
        SIZE_T VirtualSize;
        ULONG PageFaultCount;
        SIZE_T PeakWorkingSetSize;
        SIZE_T WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage;
        SIZE_T QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage;
        SIZE_T QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
        SIZE_T PrivatePageCount;
        LARGE_INTEGER ReadOperationCount;
        LARGE_INTEGER WriteOperationCount;
        LARGE_INTEGER OtherOperationCount;
        LARGE_INTEGER ReadTransferCount;
        LARGE_INTEGER WriteTransferCount;
        LARGE_INTEGER OtherTransferCount;
    };

    using NtQuerySystemInformationFn = NtStatus(NTAPI*)(ULONG systemInformationClass, PVOID systemInformation,
                                                        ULONG systemInformationLength, PULONG returnLength);

    struct Process
    {
        DWORD pid = 0;
        DWORD parentPid = 0;
        std::string name;
        LONG basePriority = 0;
        ULONG threadCount = 0;
        ULONG handleCount = 0;
        LONGLONG kernelTime = 0;
        LONGLONG userTime = 0;
        LONGLONG createTime = 0;
        ULONGLONG virtualSize = 0;
        ULONGLONG workingSet = 0;
        ULONGLONG pagefile = 0;
        ULONGLONG peakPagefile = 0;
        ULONGLONG nonPagedPool = 0;
        ULONGLONG pagedPool = 0;
        ULONG pageFaults = 0;
        const SystemThreadInfo* threads = nullptr;
    };

    // Base name the original shows: ANSI, without the .exe extension. Idle carries no image name.
    [[nodiscard]] inline std::string ProcessName(const SystemProcessInfo* info)
    {
        const DWORD pid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(info->UniqueProcessId));
        if (pid == 0)
        {
            return "Idle";
        }
        if (info->ImageName.Buffer == nullptr || info->ImageName.Length == 0)
        {
            return {};
        }
        const int wideChars = info->ImageName.Length / static_cast<int>(sizeof(WCHAR));
        const int needed =
            ::WideCharToMultiByte(CP_ACP, 0, info->ImageName.Buffer, wideChars, nullptr, 0, nullptr, nullptr);
        std::string name(static_cast<size_t>(needed), '\0');
        ::WideCharToMultiByte(CP_ACP, 0, info->ImageName.Buffer, wideChars, name.data(), needed, nullptr, nullptr);
        if (name.size() >= 4 && ::_stricmp(name.c_str() + name.size() - 4, ".exe") == 0)
        {
            name.resize(name.size() - 4);
        }
        return name;
    }

    [[nodiscard]] inline std::vector<BYTE> QueryProcesses()
    {
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return {};
        }
        const auto query =
            reinterpret_cast<NtQuerySystemInformationFn>(::GetProcAddress(ntdll, "NtQuerySystemInformation"));
        if (query == nullptr)
        {
            return {};
        }
        ULONG size = 0x20000;
        std::vector<BYTE> buffer;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            buffer.resize(size);
            ULONG needed = 0;
            const NtStatus status = query(kSystemProcessInformation, buffer.data(), size, &needed);
            if (status == kStatusInfoLengthMismatch)
            {
                size = needed != 0 ? needed + 0x10000 : size * 2;
                continue;
            }
            if (status < 0)
            {
                return {};
            }
            buffer.resize(needed);
            return buffer;
        }
        return {};
    }

    [[nodiscard]] inline std::vector<Process> CollectProcesses(const std::vector<BYTE>& raw)
    {
        std::vector<Process> result;
        size_t offset = 0;
        while (offset + sizeof(SystemProcessInfo) <= raw.size())
        {
            const auto* info = reinterpret_cast<const SystemProcessInfo*>(raw.data() + offset);
            Process p;
            p.pid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(info->UniqueProcessId));
            p.parentPid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(info->InheritedFromUniqueProcessId));
            p.name = ProcessName(info);
            p.basePriority = info->BasePriority;
            p.threadCount = info->NumberOfThreads;
            p.handleCount = info->HandleCount;
            p.kernelTime = info->KernelTime.QuadPart;
            p.userTime = info->UserTime.QuadPart;
            p.createTime = info->CreateTime.QuadPart;
            p.virtualSize = info->VirtualSize;
            p.workingSet = info->WorkingSetSize;
            p.pagefile = info->PagefileUsage;
            p.peakPagefile = info->PeakPagefileUsage;
            p.nonPagedPool = info->QuotaNonPagedPoolUsage;
            p.pagedPool = info->QuotaPagedPoolUsage;
            p.pageFaults = info->PageFaultCount;
            p.threads = reinterpret_cast<const SystemThreadInfo*>(reinterpret_cast<const BYTE*>(info) +
                                                                  sizeof(SystemProcessInfo));
            result.push_back(std::move(p));
            if (info->NextEntryOffset == 0)
            {
                break;
            }
            offset += info->NextEntryOffset;
        }
        return result;
    }
} // namespace pslist
