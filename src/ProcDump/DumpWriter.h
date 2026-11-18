// ProcDump dump writer: the dump-type flag words, the filename scheme, and the
// MiniDumpWriteDump call with its status lines (sub_140013D00 / sub_140014060 /
// sub_140014620). See analysis/ProcDump.md for the sub_ map.
#pragma once

#include "ProcDumpCommon.h"

namespace procdump
{
    // MINIDUMP_TYPE flag words as the original computes them for a modern dbghelp
    // (ImagehlpApiVersion major 6), selected by sub_140006A50. Mini is the
    // metadata-rich base without full memory, Full adds MiniDumpWithFullMemory,
    // Triage is the filtered set.
    constexpr DWORD kFlagsMini = 0x00469925;
    constexpr DWORD kFlagsFull = 0x00469927;
    constexpr DWORD kFlagsTriage = 0x001285A0;
    constexpr DWORD kFlagsMiniPlus = 0x00469925;
    constexpr DWORD kFlagsCallback = 0x00469925;

    // The original retries with MiniDumpWithIptTrace cleared when the first write
    // fails with E_INVALIDARG on a dbghelp predating that flag (sub_140014620).
    constexpr DWORD kMiniDumpWithIptTrace = 0x00400000;

    constexpr unsigned long long kBytesPerMb = 1024000ULL;

    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

    [[nodiscard]] inline DWORD DumpFlags(const Config& cfg)
    {
        switch (cfg.type)
        {
        case DumpType::Mini:
            return kFlagsMini;
        case DumpType::Full:
            return kFlagsFull;
        case DumpType::Triage:
            return kFlagsTriage;
        case DumpType::MiniPlus:
            return kFlagsMiniPlus;
        case DumpType::Callback:
            return kFlagsCallback;
        case DumpType::Custom:
            return cfg.customMask;
        }
        return kFlagsMini;
    }

    // The estimated-size "writing" status line is shown only for the Full (-ma)
    // dump. MiniPlus, Triage, Mini, and a Custom mask do not print it.
    [[nodiscard]] inline bool ShowsEstimate(const Config& cfg) noexcept
    {
        return cfg.type == DumpType::Full;
    }

    [[nodiscard]] inline std::wstring ExpandMask(const std::wstring& mask, const Target& target, DWORD exceptionCode)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t ymd[8] = {};
        wchar_t hms[8] = {};
        std::swprintf(ymd, std::size(ymd), L"%02d%02d%02d", st.wYear % 100, st.wMonth, st.wDay);
        std::swprintf(hms, std::size(hms), L"%02d%02d%02d", st.wHour, st.wMinute, st.wSecond);
        wchar_t pid[16] = {};
        std::swprintf(pid, std::size(pid), L"%u", target.pid);
        wchar_t code[16] = {};
        std::swprintf(code, std::size(code), L"%08X", exceptionCode);

        struct Sub
        {
            const wchar_t* token;
            const wchar_t* value;
        };
        const Sub subs[] = {
            {L"PROCESSNAME", target.name.c_str()},
            {L"EXCEPTIONCODE", code},
            {L"PID", pid},
            {L"YYMMDD", ymd},
            {L"HHMMSS", hms},
        };

        std::wstring out;
        for (std::wstring::size_type i = 0; i < mask.size();)
        {
            bool replaced = false;
            for (const Sub& s : subs)
            {
                const size_t len = std::wcslen(s.token);
                if (mask.compare(i, len, s.token) == 0)
                {
                    out += s.value;
                    i += len;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
            {
                out += mask[i];
                ++i;
            }
        }
        return out;
    }

    [[nodiscard]] inline bool IsDirectoryArg(const std::wstring& arg)
    {
        if (arg.empty())
        {
            return false;
        }
        const wchar_t last = arg.back();
        if (last == L'\\' || last == L'/')
        {
            return true;
        }
        const DWORD attrs = GetFileAttributesW(arg.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    // Build the dump path. A folder argument is concatenated verbatim with a
    // backslash and the expanded default mask, which reproduces the original's
    // "folder/\name" when the folder already ends in a separator. A file argument
    // is treated as a name or mask. Without an argument the current directory is
    // used.
    [[nodiscard]] inline std::wstring BuildDumpPath(const Config& cfg, const Target& target, DWORD exceptionCode)
    {
        const std::wstring defaultMask = L"PROCESSNAME_YYMMDD_HHMMSS";
        std::wstring base;
        if (cfg.hasDumpArg && !IsDirectoryArg(cfg.dumpArg))
        {
            base = ExpandMask(cfg.dumpArg, target, exceptionCode);
            if (base.size() < 4 || _wcsicmp(base.c_str() + base.size() - 4, L".dmp") != 0)
            {
                base += L".dmp";
            }
        }
        else
        {
            std::wstring folder;
            if (cfg.hasDumpArg)
            {
                folder = cfg.dumpArg;
            }
            else
            {
                wchar_t cwd[MAX_PATH] = {};
                GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
                folder = cwd;
            }
            base = folder + L"\\" + ExpandMask(defaultMask, target, exceptionCode) + L".dmp";
        }

        if (cfg.overwrite)
        {
            return base;
        }

        // Without -o the original appends -1, -2, ... before the extension on a
        // collision.
        if (GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return base;
        }
        const std::wstring::size_type dot = base.find_last_of(L'.');
        const std::wstring stem = (dot == std::wstring::npos) ? base : base.substr(0, dot);
        const std::wstring ext = (dot == std::wstring::npos) ? std::wstring() : base.substr(dot);
        for (int n = 1; n < 1000; ++n)
        {
            std::wstring candidate = stem + L"-" + std::to_wstring(n) + ext;
            if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                return candidate;
            }
        }
        return base;
    }

    // Estimate the full-dump size from the target's committed regions, for the
    // "writing: Estimated dump file size is N MB." line on full-memory types.
    [[nodiscard]] inline unsigned long long EstimateCommitted(HANDLE proc)
    {
        unsigned long long total = 0;
        MEMORY_BASIC_INFORMATION mbi{};
        unsigned char* address = nullptr;
        while (VirtualQueryEx(proc, address, &mbi, sizeof(mbi)) == sizeof(mbi))
        {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD) == 0 && mbi.Protect != PAGE_NOACCESS)
            {
                total += mbi.RegionSize;
            }
            unsigned char* next = static_cast<unsigned char*>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= address)
            {
                break;
            }
            address = next;
        }
        return total;
    }

    // Load dbghelp, resolve MiniDumpWriteDump, write one dump and print the
    // initiated / writing / complete status lines.
    [[nodiscard]] inline bool WriteOneDump(const Config& cfg, const Target& target, HANDLE proc, int dumpNumber,
                                           DWORD exceptionCode, PMINIDUMP_EXCEPTION_INFORMATION exInfo)
    {
        static MiniDumpWriteDumpFn miniDump = nullptr;
        if (miniDump == nullptr)
        {
            const HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (dbghelp == nullptr)
            {
                std::fputs("Unable to load debug help.\n", stdout);
                return false;
            }
            miniDump = reinterpret_cast<MiniDumpWriteDumpFn>(
                reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
            if (miniDump == nullptr)
            {
                std::fputs("Unable to import the 'MiniDumpWriteDump' function from debug help.\n", stdout);
                return false;
            }
        }

        const std::wstring path = BuildDumpPath(cfg, target, exceptionCode);
        std::fprintf(stdout, "%s Dump %d initiated: %s\n", TimeStamp().c_str(), dumpNumber, Narrow(path).c_str());

        ScopedHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                      cfg.overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.Valid())
        {
            std::fprintf(stdout, "Error creating dump file:\n");
            return false;
        }

        if (ShowsEstimate(cfg))
        {
            const unsigned long long estimate = EstimateCommitted(proc);
            const unsigned long mb = static_cast<unsigned long>((estimate + kBytesPerMb - 1) / kBytesPerMb);
            std::fprintf(stdout, "%s Dump %d writing: Estimated dump file size is %lu MB.\n", TimeStamp().c_str(),
                         dumpNumber, mb);
        }

        DWORD flags = DumpFlags(cfg);
        const DWORD start = GetTickCount();
        BOOL ok = miniDump(proc, target.pid, file.Get(), static_cast<MINIDUMP_TYPE>(flags), exInfo, nullptr, nullptr);
        if (ok == 0)
        {
            DWORD err = GetLastError();
            if (err > 0)
            {
                err = (err & 0xFFFF) | 0x80070000;
            }
            if (err == 0x80070057 && (flags & kMiniDumpWithIptTrace) != 0)
            {
                flags &= ~kMiniDumpWithIptTrace;
                SetFilePointer(file.Get(), 0, nullptr, FILE_BEGIN);
                ok =
                    miniDump(proc, target.pid, file.Get(), static_cast<MINIDUMP_TYPE>(flags), exInfo, nullptr, nullptr);
            }
            if (ok == 0)
            {
                const DWORD finalErr = GetLastError();
                DWORD shown = finalErr;
                if (shown > 0)
                {
                    shown = (shown & 0xFFFF) | 0x80070000;
                }
                std::fprintf(stdout, "%s Dump %d error: Error writing dump file: 0x%08X\n", TimeStamp().c_str(),
                             dumpNumber, shown);
                return false;
            }
        }
        const DWORD elapsed = GetTickCount() - start;

        LARGE_INTEGER fileSize{};
        GetFileSizeEx(file.Get(), &fileSize);
        const unsigned long mb = static_cast<unsigned long>(
            (static_cast<unsigned long long>(fileSize.QuadPart) + kBytesPerMb - 1) / kBytesPerMb);
        std::fprintf(stdout, "%s Dump %d complete: %lu MB written in %0.1f seconds\n", TimeStamp().c_str(), dumpNumber,
                     mb, static_cast<double>(elapsed) / 1000.0);
        return true;
    }
} // namespace procdump
