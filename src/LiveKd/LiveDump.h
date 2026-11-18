// LiveKd native live kernel dump (sub_140009830): the SYSDBG_LIVEDUMP_CONTROL
// layout and flags, the restrictive dump-file DACL (sub_140002320), and the
// NtSystemDebugControl(SysDbgGetLiveKernelDump) writer. This is the safe path: it
// snapshots kernel memory without bugchecking. See analysis/LiveKd.md.
#pragma once

#include "Startup.h"

namespace livekd
{
    // NtSystemDebugControl command for the native live kernel dump.
    constexpr ULONG kSysDbgGetLiveKernelDump = 37;

    // SYSDBG_LIVEDUMP_CONTROL flag bits (sub_140009830).
    constexpr ULONG kFlagUseDumpStorageStack = 0x1;
    constexpr ULONG kFlagIncludeUserSpacePages = 0x4;
    constexpr ULONG kAddPagesHypervisor = 0x1;

    // Restrictive DACL for the dump file: full access to SYSTEM and the local
    // Administrators group only. The dump holds kernel memory, so it is secured
    // against other principals (sub_140002320).
    constexpr wchar_t kDumpSddl[] = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";

    // Default temp dump written in the launch path (main / sub_140002180).
    constexpr char kDefaultDumpName[] = "livekd.dmp";

    using NtSystemDebugControlFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);

    // SYSDBG_LIVEDUMP_CONTROL as laid out by the original (64 bytes).
    struct SysDbgLiveDumpControl
    {
        ULONG version = 0;
        ULONG bugCheckCode = 0;
        ULONG_PTR bugCheckParam1 = 0;
        ULONG_PTR bugCheckParam2 = 0;
        ULONG_PTR bugCheckParam3 = 0;
        ULONG_PTR bugCheckParam4 = 0;
        HANDLE dumpFileHandle = nullptr;
        HANDLE cancelEventHandle = nullptr;
        ULONG flags = 0;
        ULONG addPagesControl = 0;
    };
    static_assert(sizeof(SysDbgLiveDumpControl) == 64, "SYSDBG_LIVEDUMP_CONTROL must be 64 bytes");
    static_assert(offsetof(SysDbgLiveDumpControl, dumpFileHandle) == 0x28, "DumpFileHandle offset");
    static_assert(offsetof(SysDbgLiveDumpControl, cancelEventHandle) == 0x30, "CancelEventHandle offset");
    static_assert(offsetof(SysDbgLiveDumpControl, flags) == 0x38, "Flags offset");
    static_assert(offsetof(SysDbgLiveDumpControl, addPagesControl) == 0x3C, "AddPagesControl offset");

    [[nodiscard]] inline bool BuildDumpSecurity(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd)
    {
        sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kDumpSddl, SDDL_REVISION_1, &sd, nullptr))
        {
            return false;
        }
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
        return true;
    }

    // Native live kernel dump (sub_140009830). Resolves NtSystemDebugControl,
    // creates the secured dump file, fills SYSDBG_LIVEDUMP_CONTROL and calls
    // SysDbgGetLiveKernelDump.
    [[nodiscard]] inline bool WriteLiveKernelDump(const std::string& path, HANDLE processHandle, bool normalFileAttr,
                                                  bool hypervisorPages, bool useDumpStorageStack)
    {
        if (!EnablePrivilege(L"SeDebugPrivilege"))
        {
            std::fputs("Unable to enable SeDebugPrivilege.  Administrator rights are required to use LiveKd.\n",
                       stdout);
            return false;
        }

        ScopedHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!cancelEvent.Valid())
        {
            std::fprintf(stdout, "CreateEvent failed - error %lu.\n", GetLastError());
            return false;
        }

        const HMODULE ntdll = GetModuleHandleW(L"NTDLL.DLL");
        const auto ntSystemDebugControl =
            ntdll != nullptr ? reinterpret_cast<NtSystemDebugControlFn>(
                                   reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSystemDebugControl")))
                             : nullptr;
        if (ntSystemDebugControl == nullptr)
        {
            std::fputs("Failed to link to NTDLL!NtSystemDebugControl.\n", stdout);
            return false;
        }

        SECURITY_ATTRIBUTES sa{};
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!BuildDumpSecurity(sa, sd))
        {
            std::fputs("Error preparing livekd dump file security:\n", stdout);
            return false;
        }

        const DWORD attrs = normalFileAttr ? FILE_ATTRIBUTE_NORMAL : (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        ScopedHandle dumpFile(CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE | WRITE_DAC,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS,
                                          attrs, nullptr));
        LocalFree(sd);
        if (!dumpFile.Valid())
        {
            std::fputs("Error creating livekd dump data:\n", stdout);
            return false;
        }

        SysDbgLiveDumpControl control;
        control.version = 1;
        control.dumpFileHandle = dumpFile.Get();
        control.cancelEventHandle = cancelEvent.Get();
        if (processHandle != nullptr)
        {
            control.flags |= kFlagIncludeUserSpacePages;
        }
        if (useDumpStorageStack)
        {
            control.flags |= kFlagUseDumpStorageStack;
        }
        if (hypervisorPages)
        {
            control.addPagesControl |= kAddPagesHypervisor;
        }

        std::fprintf(stdout, "Saving live dump to %s...", path.c_str());
        std::fflush(stdout);
        const LONG status =
            ntSystemDebugControl(kSysDbgGetLiveKernelDump, &control, sizeof(control), nullptr, 0, nullptr);
        if (status >= 0)
        {
            std::fputs(" done.\n", stdout);
            return true;
        }
        std::fprintf(stdout, " failed (status %08X).\n", static_cast<unsigned int>(status));
        return false;
    }
} // namespace livekd
