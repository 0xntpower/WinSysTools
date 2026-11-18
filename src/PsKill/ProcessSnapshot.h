// PsKill process snapshot and tree walk: the NtQuerySystemInformation record
// layout, the enumeration (sub_1400045F0), the name match, and the ancestry walk
// used by the tree kill (sub_1400046A0). See analysis/PsKill.md for the sub_ map.
#pragma once

#include "PsKillCommon.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pskill
{
    // NtQuerySystemInformation information class for the process list.
    constexpr ULONG kSystemProcessInformation = 5;

    using NtQuerySystemInformationFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

    // windows.h does not declare UNICODE_STRING, and winternl.h would collide with
    // the struct below, so the counted string is spelled out here. On x64 the
    // 4-byte gap after MaximumLength puts Buffer at offset 8, matching the OS.
    struct CountedUnicodeString
    {
        USHORT Length = 0;
        USHORT MaximumLength = 0;
        PWSTR Buffer = nullptr;
    };

    // Layout of SYSTEM_PROCESS_INFORMATION on x64. Only the fields the tool reads
    // are named, the rest are padding chosen so the named offsets land exactly
    // where the original indexes them (PID at 0x50, parent at 0x58, creation time
    // at 0x20, image-name UNICODE_STRING at 0x38).
    struct SystemProcessInformation
    {
        ULONG nextEntryOffset;            // 0x00
        ULONG numberOfThreads;            // 0x04
        LARGE_INTEGER workingSetPrivate;  // 0x08
        ULONG hardFaultCount;             // 0x10
        ULONG numberOfThreadsHighWater;   // 0x14
        ULONGLONG cycleTime;              // 0x18
        LARGE_INTEGER createTime;         // 0x20
        LARGE_INTEGER userTime;           // 0x28
        LARGE_INTEGER kernelTime;         // 0x30
        CountedUnicodeString imageName;   // 0x38
        LONG basePriority;                // 0x48
        ULONG_PTR uniqueProcessId;        // 0x50
        ULONG_PTR inheritedFromProcessId; // 0x58
    };

    static_assert(offsetof(SystemProcessInformation, createTime) == 0x20);
    static_assert(offsetof(SystemProcessInformation, imageName) == 0x38);
    static_assert(offsetof(SystemProcessInformation, uniqueProcessId) == 0x50);
    static_assert(offsetof(SystemProcessInformation, inheritedFromProcessId) == 0x58);

    // One row of the process snapshot in the form the tool actually uses.
    struct ProcessRecord
    {
        DWORD pid = 0;
        DWORD parentPid = 0;
        long long createTime = 0;
        std::string name;
    };

    // Build the process snapshot through NtQuerySystemInformation. Mirrors
    // sub_1400045F0, which grows the buffer until the query fits.
    [[nodiscard]] inline std::vector<ProcessRecord> SnapshotProcesses()
    {
        std::vector<ProcessRecord> records;
        const HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        if (ntdll == nullptr)
        {
            return records;
        }
        const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtQuerySystemInformation")));
        if (query == nullptr)
        {
            return records;
        }

        ULONG size = 1000;
        std::vector<BYTE> buffer(size);
        ULONG needed = 0;
        while (query(kSystemProcessInformation, buffer.data(), size, &needed) != 0)
        {
            size += 10000;
            buffer.assign(size, 0);
        }

        for (ULONG offset = 0;;)
        {
            const auto* entry = reinterpret_cast<const SystemProcessInformation*>(buffer.data() + offset);
            ProcessRecord record;
            record.pid = static_cast<DWORD>(entry->uniqueProcessId);
            record.parentPid = static_cast<DWORD>(entry->inheritedFromProcessId);
            record.createTime = entry->createTime.QuadPart;
            if (entry->imageName.Buffer != nullptr && entry->imageName.Length != 0)
            {
                const int chars = entry->imageName.Length / static_cast<USHORT>(sizeof(WCHAR));
                const int bytes =
                    ::WideCharToMultiByte(CP_ACP, 0, entry->imageName.Buffer, chars, nullptr, 0, nullptr, nullptr);
                if (bytes > 0)
                {
                    record.name.resize(static_cast<size_t>(bytes));
                    ::WideCharToMultiByte(CP_ACP, 0, entry->imageName.Buffer, chars, record.name.data(), bytes, nullptr,
                                          nullptr);
                }
            }
            records.push_back(std::move(record));

            if (entry->nextEntryOffset == 0)
            {
                break;
            }
            offset += entry->nextEntryOffset;
        }
        return records;
    }

    // A process name matches the target when the target is a case-insensitive
    // prefix and the next character is a dot or the end of the name, so "notepad"
    // matches "notepad.exe". Mirrors the strnicmp plus trailing '.' or '\0' test.
    [[nodiscard]] inline bool NameMatches(const std::string& procName, const char* target)
    {
        const size_t len = std::strlen(target);
        if (::_strnicmp(procName.c_str(), target, len) != 0)
        {
            return false;
        }
        const char next = procName.c_str()[len];
        return next == '.' || next == '\0';
    }

    [[nodiscard]] inline const ProcessRecord* FindByPid(const std::vector<ProcessRecord>& procs, DWORD pid)
    {
        for (const auto& p : procs)
        {
            if (p.pid == pid)
            {
                return &p;
            }
        }
        return nullptr;
    }

    // Is candidate the root itself, or a descendant of it? The parent chain of
    // the candidate is climbed until it reaches the root (descendant) or runs out
    // (unrelated). Mirrors the ancestry walk in sub_1400046A0.
    [[nodiscard]] inline bool IsInTree(const std::vector<ProcessRecord>& procs, DWORD candidate, DWORD root)
    {
        DWORD current = candidate;
        for (size_t guard = 0; guard <= procs.size(); ++guard)
        {
            if (current == root)
            {
                return true;
            }
            const ProcessRecord* p = FindByPid(procs, current);
            if (p == nullptr || p->parentPid == 0)
            {
                return false;
            }
            current = p->parentPid;
        }
        return false;
    }

    // Collect the PIDs to terminate for a root, applying the creation-time guard
    // from the original: only a process created at or after the root can be part
    // of the root's tree. The root itself is always included.
    inline void CollectTree(const std::vector<ProcessRecord>& procs, DWORD rootPid, long long rootTime,
                            std::set<DWORD>& out)
    {
        for (const auto& p : procs)
        {
            if (p.createTime >= rootTime && IsInTree(procs, p.pid, rootPid))
            {
                out.insert(p.pid);
            }
        }
    }
} // namespace pskill
