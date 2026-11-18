// ProcessData.h -- the live process data engine.
//
// One NtQuerySystemInformation(SystemProcessInformation) call yields every
// process with its CPU times, memory counters, handle and thread counts, and
// the threads inline. CPU percent is the kernel+user time delta over the
// refresh interval against wall time times the processor count, matching how
// the original derives its CPU column. Per-process metadata (image path,
// description, company, user) is read once and cached by process identity so
// each refresh only pays for new processes.
#pragma once

#include "NtApi.h"
#include <psapi.h>
#include <winsvc.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

#pragma comment(lib, "advapi32.lib")

namespace pe
{
    // Suspended wait reason from the native thread state.
    inline constexpr ULONG kWaitReasonSuspended = 5;
    inline constexpr ULONG kThreadStateWaiting = 5;

    struct ThreadRow
    {
        DWORD tid = 0;
        uint64_t startAddr = 0;
        double cpu = 0.0;
        uint64_t cpuTime = 0; // kernel+user, 100ns units
        ULONG state = 0;
    };

    struct ProcRow
    {
        DWORD pid = 0;
        DWORD parentPid = 0;
        uint64_t createTime = 0; // 100ns FILETIME
        std::wstring name;
        double cpu = 0.0;
        uint64_t privateBytes = 0;
        uint64_t workingSet = 0;
        uint64_t virtualSize = 0;
        ULONG handleCount = 0;
        ULONG threadCount = 0;
        DWORD sessionId = 0;

        std::wstring imagePath;
        std::wstring description;
        std::wstring company;
        std::wstring version;
        std::wstring user;

        bool isOwn = false;
        bool isService = false;
        bool isSuspended = false;

        // Difference-highlight bookkeeping, filled by the view layer.
        bool isNew = false;
        bool isDead = false;
        uint64_t flashUntil = 0;

        std::vector<ThreadRow> threads;

        [[nodiscard]] uint64_t Key() const
        {
            return (static_cast<uint64_t>(pid) << 20) ^ createTime;
        }
    };

    // Per-process metadata resolved once and reused while the process lives.
    struct Meta
    {
        std::wstring imagePath;
        std::wstring description;
        std::wstring company;
        std::wstring version;
        std::wstring user;
        bool resolved = false;
    };

    [[nodiscard]] inline uint64_t NowFileTime()
    {
        FILETIME ft{};
        ::GetSystemTimeAsFileTime(&ft);
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    [[nodiscard]] inline uint64_t ToU64(const LARGE_INTEGER& li)
    {
        return (static_cast<uint64_t>(li.HighPart) << 32) | li.LowPart;
    }

    // Read FileDescription and CompanyName from an image's version info.
    inline void ReadVersionInfo(const std::wstring& path, Meta& m)
    {
        if (path.empty())
        {
            return;
        }
        DWORD handle = 0;
        const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
        if (size == 0)
        {
            return;
        }
        std::vector<BYTE> block(size);
        if (!::GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
        {
            return;
        }
        struct LangCp
        {
            WORD lang;
            WORD cp;
        };
        LangCp* langs = nullptr;
        UINT langBytes = 0;
        if (!::VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs),
                              &langBytes) ||
            langBytes < sizeof(LangCp))
        {
            return;
        }
        const int count = static_cast<int>(langBytes / sizeof(LangCp));
        auto query = [&](const wchar_t* field) -> std::wstring
        {
            for (int i = 0; i < count; ++i)
            {
                wchar_t sub[128] = {};
                swprintf(sub, 128, L"\\StringFileInfo\\%04x%04x\\%s", langs[i].lang, langs[i].cp, field);
                wchar_t* val = nullptr;
                UINT len = 0;
                if (::VerQueryValueW(block.data(), sub, reinterpret_cast<void**>(&val), &len) && val && len)
                {
                    return std::wstring(val, (len && val[len - 1] == 0) ? len - 1 : len);
                }
            }
            return std::wstring();
        };
        m.description = query(L"FileDescription");
        m.company = query(L"CompanyName");
        m.version = query(L"ProductVersion");
        if (m.version.empty())
        {
            m.version = query(L"FileVersion");
        }
    }

    // Resolve the account name that owns a process token, as DOMAIN\User.
    inline std::wstring ProcessUser(HANDLE hProc)
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(hProc, TOKEN_QUERY, &token))
        {
            return std::wstring();
        }
        DWORD len = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
        std::wstring out;
        if (len)
        {
            std::vector<BYTE> buf(len);
            if (::GetTokenInformation(token, TokenUser, buf.data(), len, &len))
            {
                auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                wchar_t nameBuf[256] = {};
                wchar_t domBuf[256] = {};
                DWORD nameLen = 256, domLen = 256;
                SID_NAME_USE use{};
                if (::LookupAccountSidW(nullptr, tu->User.Sid, nameBuf, &nameLen, domBuf, &domLen, &use))
                {
                    out = domBuf;
                    if (!out.empty())
                    {
                        out += L'\\';
                    }
                    out += nameBuf;
                }
            }
        }
        ::CloseHandle(token);
        return out;
    }

    // Query a process command line through ProcessCommandLineInformation
    // (Windows 8.1 and later). Best effort, returns empty on failure.
    inline std::wstring ProcessCommandLine(HANDLE hProc)
    {
        const ntx::Api& api = ntx::Nt();
        if (!api.QueryInformationProcess)
        {
            return std::wstring();
        }
        ULONG needed = 0;
        ntx::NTSTATUS st = api.QueryInformationProcess(hProc, ntx::kProcessCommandLineInformation, nullptr, 0, &needed);
        if (needed == 0)
        {
            return std::wstring();
        }
        std::vector<BYTE> buf(needed);
        st = api.QueryInformationProcess(hProc, ntx::kProcessCommandLineInformation, buf.data(), needed, &needed);
        if (st != ntx::kStatusSuccess)
        {
            return std::wstring();
        }
        auto* us = reinterpret_cast<ntx::UnicodeString*>(buf.data());
        if (!us->Buffer || us->Length == 0)
        {
            return std::wstring();
        }
        return std::wstring(us->Buffer, us->Length / sizeof(wchar_t));
    }

    // Read a process current directory out of its PEB. Needs VM read access,
    // returns empty when that is unavailable.
    inline std::wstring ProcessCurrentDir(HANDLE hProc)
    {
        const ntx::Api& api = ntx::Nt();
        if (!api.QueryInformationProcess)
        {
            return std::wstring();
        }
        ntx::ProcessBasicInformationBlock pbi{};
        ULONG got = 0;
        if (api.QueryInformationProcess(hProc, ntx::kProcessBasicInformation, &pbi, sizeof(pbi), &got) !=
                ntx::kStatusSuccess ||
            !pbi.PebBaseAddress)
        {
            return std::wstring();
        }
        // PEB.ProcessParameters at offset 0x20 on x64.
        BYTE* procParams = nullptr;
        SIZE_T rd = 0;
        if (!::ReadProcessMemory(hProc, reinterpret_cast<BYTE*>(pbi.PebBaseAddress) + 0x20, &procParams,
                                 sizeof(procParams), &rd) ||
            !procParams)
        {
            return std::wstring();
        }
        // RTL_USER_PROCESS_PARAMETERS.CurrentDirectory.DosPath at offset 0x38 on x64.
        ntx::UnicodeString dir{};
        if (!::ReadProcessMemory(hProc, procParams + 0x38, &dir, sizeof(dir), &rd) || !dir.Buffer || !dir.Length)
        {
            return std::wstring();
        }
        std::wstring out(dir.Length / sizeof(wchar_t), L'\0');
        if (!::ReadProcessMemory(hProc, dir.Buffer, out.data(), dir.Length, &rd))
        {
            return std::wstring();
        }
        return out;
    }

    // The live engine. Holds the previous CPU times, the metadata cache and
    // the service-process set so that each Refresh does the minimum work.
    class Engine
    {
      public:
        Engine()
        {
            SYSTEM_INFO si{};
            ::GetSystemInfo(&si);
            m_cpuCount = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
            CaptureOwnSid();
        }

        [[nodiscard]] DWORD CpuCount() const
        {
            return m_cpuCount;
        }

        std::vector<ProcRow> Refresh()
        {
            std::vector<ProcRow> rows;
            std::vector<BYTE> buf = ntx::QuerySystem(ntx::kSystemProcessInformation);
            if (buf.empty())
            {
                return rows;
            }

            RefreshServiceSet();
            const uint64_t now = NowFileTime();
            const uint64_t wall = (m_prevTime && now > m_prevTime) ? (now - m_prevTime) : 0;

            std::unordered_map<DWORD, uint64_t> newCpu;
            std::unordered_set<uint64_t> seen;

            const BYTE* p = buf.data();
            for (;;)
            {
                auto* spi = reinterpret_cast<const ntx::SystemProcessInformation*>(p);
                ProcRow row;
                row.pid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(spi->UniqueProcessId));
                row.parentPid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(spi->InheritedFromUniqueProcessId));
                row.createTime = ToU64(spi->CreateTime);
                row.privateBytes = spi->PrivatePageCount;
                row.workingSet = spi->WorkingSetSize;
                row.virtualSize = spi->VirtualSize;
                row.handleCount = spi->HandleCount;
                row.threadCount = spi->NumberOfThreads;
                row.sessionId = spi->SessionId;

                if (spi->ImageName.Buffer && spi->ImageName.Length)
                {
                    row.name.assign(spi->ImageName.Buffer, spi->ImageName.Length / sizeof(wchar_t));
                }
                else if (row.pid == 0)
                {
                    row.name = L"System Idle Process";
                }
                else if (row.pid == 4)
                {
                    row.name = L"System";
                }

                const uint64_t cpuTime = ToU64(spi->KernelTime) + ToU64(spi->UserTime);
                newCpu[row.pid] = cpuTime;
                row.cpu = CpuPercent(row.pid, cpuTime, wall);

                // Threads follow the process record inline.
                auto* thr = reinterpret_cast<const ntx::SystemThreadInformation*>(spi + 1);
                bool anyNotSuspended = false;
                for (ULONG i = 0; i < spi->NumberOfThreads; ++i)
                {
                    ThreadRow t;
                    t.tid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(thr[i].Cid.UniqueThread));
                    t.startAddr = reinterpret_cast<uint64_t>(thr[i].StartAddress);
                    t.cpuTime = ToU64(thr[i].KernelTime) + ToU64(thr[i].UserTime);
                    t.state = thr[i].ThreadState;
                    t.cpu = ThreadCpuPercent(t.tid, t.cpuTime, wall);
                    row.threads.push_back(t);
                    if (!(thr[i].ThreadState == kThreadStateWaiting && thr[i].WaitReason == kWaitReasonSuspended))
                    {
                        anyNotSuspended = true;
                    }
                }
                row.isSuspended = (spi->NumberOfThreads > 0) && !anyNotSuspended;
                row.isService = m_servicePids.count(row.pid) != 0;

                ApplyMeta(row);
                seen.insert(row.Key());
                rows.push_back(std::move(row));

                if (spi->NextEntryOffset == 0)
                {
                    break;
                }
                p += spi->NextEntryOffset;
            }

            m_prevCpu.swap(newCpu);
            m_prevTime = now;
            PruneMeta(seen);
            return rows;
        }

      private:
        DWORD m_cpuCount = 1;
        uint64_t m_prevTime = 0;
        std::unordered_map<DWORD, uint64_t> m_prevCpu;
        std::unordered_map<DWORD, uint64_t> m_prevThreadCpu;
        std::unordered_map<uint64_t, Meta> m_meta;
        std::unordered_set<DWORD> m_servicePids;
        std::vector<BYTE> m_ownSid;

        double CpuPercent(DWORD pid, uint64_t cpuTime, uint64_t wall)
        {
            if (wall == 0)
            {
                return 0.0;
            }
            auto it = m_prevCpu.find(pid);
            if (it == m_prevCpu.end() || cpuTime < it->second)
            {
                return 0.0;
            }
            const uint64_t delta = cpuTime - it->second;
            const double pct = 100.0 * static_cast<double>(delta) / (static_cast<double>(wall) * m_cpuCount);
            if (pct < 0.0)
            {
                return 0.0;
            }
            if (pct > 100.0)
            {
                return 100.0;
            }
            return pct;
        }

        double ThreadCpuPercent(DWORD tid, uint64_t cpuTime, uint64_t wall)
        {
            double pct = 0.0;
            if (wall != 0)
            {
                auto it = m_prevThreadCpu.find(tid);
                if (it != m_prevThreadCpu.end() && cpuTime >= it->second)
                {
                    pct = 100.0 * static_cast<double>(cpuTime - it->second) / (static_cast<double>(wall) * m_cpuCount);
                }
            }
            m_prevThreadCpu[tid] = cpuTime;
            if (pct < 0.0)
            {
                return 0.0;
            }
            if (pct > 100.0)
            {
                return 100.0;
            }
            return pct;
        }

        void CaptureOwnSid()
        {
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
            {
                return;
            }
            DWORD len = 0;
            ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
            if (len)
            {
                std::vector<BYTE> buf(len);
                if (::GetTokenInformation(token, TokenUser, buf.data(), len, &len))
                {
                    auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                    const DWORD sidLen = ::GetLengthSid(tu->User.Sid);
                    m_ownSid.assign(reinterpret_cast<BYTE*>(tu->User.Sid),
                                    reinterpret_cast<BYTE*>(tu->User.Sid) + sidLen);
                }
            }
            ::CloseHandle(token);
        }

        [[nodiscard]] bool IsOwnToken(HANDLE hProc) const
        {
            if (m_ownSid.empty())
            {
                return false;
            }
            HANDLE token = nullptr;
            if (!::OpenProcessToken(hProc, TOKEN_QUERY, &token))
            {
                return false;
            }
            DWORD len = 0;
            ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
            bool own = false;
            if (len)
            {
                std::vector<BYTE> buf(len);
                if (::GetTokenInformation(token, TokenUser, buf.data(), len, &len))
                {
                    auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                    own = ::EqualSid(tu->User.Sid, const_cast<PSID>(reinterpret_cast<const void*>(m_ownSid.data()))) !=
                          FALSE;
                }
            }
            ::CloseHandle(token);
            return own;
        }

        void ApplyMeta(ProcRow& row)
        {
            const uint64_t key = row.Key();
            auto it = m_meta.find(key);
            if (it == m_meta.end())
            {
                Meta m;
                HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid);
                if (hProc)
                {
                    wchar_t path[MAX_PATH] = {};
                    DWORD cch = MAX_PATH;
                    if (::QueryFullProcessImageNameW(hProc, 0, path, &cch))
                    {
                        m.imagePath.assign(path, cch);
                    }
                    m.user = ProcessUser(hProc);
                    row.isOwn = IsOwnToken(hProc);
                    ::CloseHandle(hProc);
                }
                ReadVersionInfo(m.imagePath, m);
                m.resolved = true;
                it = m_meta.emplace(key, std::move(m)).first;
                m_ownFlag[key] = row.isOwn;
            }
            const Meta& m = it->second;
            row.imagePath = m.imagePath;
            row.description = m.description;
            row.company = m.company;
            row.version = m.version;
            row.user = m.user;
            auto of = m_ownFlag.find(key);
            if (of != m_ownFlag.end())
            {
                row.isOwn = of->second;
            }
        }

        void PruneMeta(const std::unordered_set<uint64_t>& seen)
        {
            for (auto it = m_meta.begin(); it != m_meta.end();)
            {
                it = seen.count(it->first) ? std::next(it) : m_meta.erase(it);
            }
            for (auto it = m_ownFlag.begin(); it != m_ownFlag.end();)
            {
                it = seen.count(it->first) ? std::next(it) : m_ownFlag.erase(it);
            }
        }

        void RefreshServiceSet()
        {
            m_servicePids.clear();
            SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
            if (!scm)
            {
                return;
            }
            DWORD needed = 0, returned = 0, resume = 0;
            ::EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &needed,
                                    &returned, &resume, nullptr);
            if (needed)
            {
                std::vector<BYTE> buf(needed);
                resume = 0;
                if (::EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buf.data(),
                                            needed, &needed, &returned, &resume, nullptr))
                {
                    auto* svc = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
                    for (DWORD i = 0; i < returned; ++i)
                    {
                        if (svc[i].ServiceStatusProcess.dwProcessId)
                        {
                            m_servicePids.insert(svc[i].ServiceStatusProcess.dwProcessId);
                        }
                    }
                }
            }
            ::CloseServiceHandle(scm);
        }

        std::unordered_map<uint64_t, bool> m_ownFlag;
    };
} // namespace pe
