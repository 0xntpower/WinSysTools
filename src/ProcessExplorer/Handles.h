// Handles.h -- the handle list for one process.
//
// SystemExtendedHandleInformation returns every handle on the machine with its
// owning PID and object-type index in one call, so the pane filters that set
// to the selected process. Each kept handle is duplicated into this process so
// NtQueryObject can resolve its type and name. The name query is skipped for
// the synchronous pipe access mask that is known to block NtQueryObject, which
// is the safe no-thread form of the original's timeout-guarded query.
#pragma once

#include "NtApi.h"
#include <string>
#include <vector>

namespace pe
{
    // GrantedAccess value of a synchronous pipe/file handle whose name query
    // can block NtQueryObject indefinitely. The original runs the name query on
    // a watchdog thread, here the known-bad mask is simply skipped.
    inline constexpr ULONG kSyncPipeAccess = 0x0012019F;

    struct HandleRow
    {
        std::wstring type;
        std::wstring name;
        uint64_t     handle = 0;
        ULONG        access = 0;
    };

    // Resolve a duplicated handle's object type name.
    inline std::wstring QueryHandleType(HANDLE h)
    {
        const ntx::Api& api = ntx::Nt();
        if (!api.QueryObject)
        {
            return std::wstring();
        }
        BYTE buf[sizeof(ntx::ObjectTypeInformation) + 256] = {};
        ULONG len = 0;
        if (api.QueryObject(h, ntx::kObjectTypeInformation, buf, sizeof(buf), &len) != ntx::kStatusSuccess)
        {
            return std::wstring();
        }
        auto* ti = reinterpret_cast<ntx::ObjectTypeInformation*>(buf);
        if (!ti->TypeName.Buffer || !ti->TypeName.Length)
        {
            return std::wstring();
        }
        return std::wstring(ti->TypeName.Buffer, ti->TypeName.Length / sizeof(wchar_t));
    }

    // Resolve a duplicated handle's object name. Skipped for the sync pipe
    // access mask to avoid the known NtQueryObject hang.
    inline std::wstring QueryHandleName(HANDLE h, ULONG grantedAccess)
    {
        const ntx::Api& api = ntx::Nt();
        if (!api.QueryObject || grantedAccess == kSyncPipeAccess)
        {
            return std::wstring();
        }
        BYTE stackBuf[sizeof(ntx::ObjectNameInformation) + 1024] = {};
        ULONG len = 0;
        ntx::NTSTATUS st = api.QueryObject(h, ntx::kObjectNameInformation, stackBuf, sizeof(stackBuf), &len);
        if (st == ntx::kStatusSuccess)
        {
            auto* ni = reinterpret_cast<ntx::ObjectNameInformation*>(stackBuf);
            if (ni->Name.Buffer && ni->Name.Length)
            {
                return std::wstring(ni->Name.Buffer, ni->Name.Length / sizeof(wchar_t));
            }
            return std::wstring();
        }
        if (len > sizeof(stackBuf))
        {
            std::vector<BYTE> heap(len);
            if (api.QueryObject(h, ntx::kObjectNameInformation, heap.data(), len, &len) == ntx::kStatusSuccess)
            {
                auto* ni = reinterpret_cast<ntx::ObjectNameInformation*>(heap.data());
                if (ni->Name.Buffer && ni->Name.Length)
                {
                    return std::wstring(ni->Name.Buffer, ni->Name.Length / sizeof(wchar_t));
                }
            }
        }
        return std::wstring();
    }

    [[nodiscard]] inline std::vector<HandleRow> EnumHandles(DWORD pid)
    {
        std::vector<HandleRow> rows;
        std::vector<BYTE> buf = ntx::QuerySystem(ntx::kSystemExtendedHandleInformation, 0x100000);
        if (buf.empty())
        {
            return rows;
        }
        HANDLE hProc = ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        if (!hProc)
        {
            return rows;
        }
        auto* info = reinterpret_cast<ntx::SystemHandleInformationEx*>(buf.data());
        const HANDLE self = ::GetCurrentProcess();
        for (ULONG_PTR i = 0; i < info->NumberOfHandles; ++i)
        {
            const ntx::SystemHandleTableEntryInfoEx& e = info->Handles[i];
            if (e.UniqueProcessId != pid)
            {
                continue;
            }
            HandleRow row;
            row.handle = e.HandleValue;
            row.access = e.GrantedAccess;

            HANDLE dup = nullptr;
            if (::DuplicateHandle(hProc, reinterpret_cast<HANDLE>(e.HandleValue),
                                  self, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS) && dup)
            {
                row.type = QueryHandleType(dup);
                row.name = QueryHandleName(dup, e.GrantedAccess);
                ::CloseHandle(dup);
            }
            rows.push_back(std::move(row));
        }
        ::CloseHandle(hProc);
        return rows;
    }
}
