// LiveKd - console recreation of the Sysinternals LiveKd utility (v5.65).
//
// Runs the kernel debugger (kd or windbg from the Debugging Tools for Windows)
// against the live system, or writes a live kernel dump to disk. Reverse
// engineered from targets/sysinternals/livekd64.exe. The banner (sub_14000BFE0),
// EULA gate (sub_1400018B0), admin and privilege checks (sub_140008170 /
// sub_140002240), argument parser and dispatch (main, sub_14000BBA0 usage),
// debugger-path discovery (sub_14000B7D0 / sub_14000B570), symbol-path setup
// (sub_14000AAD0) and the native live-dump writer (sub_140009830) were all
// recovered and matched against the original.
//
// Scope and safety: this recreation reproduces only the modern, safe live-dump
// path, NtSystemDebugControl with SysDbgGetLiveKernelDump (command 37), which
// snapshots kernel memory without bugchecking the machine. The original's
// legacy local-kernel-debugging driver path (LiveKdD.SYS, the DeviceIoControl
// 0x2345xxxx codes in sub_140009BE0), the mirror dump, and Hyper-V VM debugging
// (-hv / -hvl / -hvkl / -p) are deliberately out of scope and documented in
// analysis/LiveKd.md. Nothing here can crash, bugcheck, reboot or power off the
// system.

#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // --- Banner. Line one is byte-identical to the original (sub_14000BFE0).
    // The original's "Sysinternals" and copyright lines are replaced with the
    // recreation credit, matching the sibling console tools in this repo. ---
    constexpr char kBannerLine1[] =
        "\nLiveKd v5.65 - Execute kd/windbg on a live system\n";
    constexpr char kBannerCredit[] =
        "Independent source-accurate recreation of LiveKd by Sysinternals, through reverse engineering and rewritten in modern C++20.\n";
    constexpr char kBannerAuthor[] = "Author: 0xntpower\n\n";

    constexpr int kExitError = -1;

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

    using NtSystemDebugControlFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);
    using RtlGetVersionFn = LONG(NTAPI*)(PRTL_OSVERSIONINFOW);

    // --- RAII wrapper around a Win32 handle (shared shape with ProcDump). ---
    class ScopedHandle
    {
    public:
        ScopedHandle() noexcept = default;
        explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
        ~ScopedHandle()
        {
            Close();
        }
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;
        ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }
        ScopedHandle& operator=(ScopedHandle&& other) noexcept
        {
            if (this != &other)
            {
                Close();
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }
        void Reset(HANDLE handle) noexcept
        {
            Close();
            handle_ = handle;
        }
        [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
        [[nodiscard]] bool Valid() const noexcept
        {
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

    private:
        void Close() noexcept
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
            handle_ = nullptr;
        }
        HANDLE handle_ = nullptr;
    };

    struct Config
    {
        bool useWindbg = false;         // -w
        bool batch = false;             // -b
        bool dumpToFile = false;        // -o <file>
        std::string dumpFile;           // -o argument (fully qualified)
        bool nativeLive = false;        // -ml
        bool hypervisorPages = false;   // -hvd
        bool dumpStorageStack = false;  // -md
        bool verboseSymbols = false;    // -vsym
        std::string debuggerPath;       // -k <path>
        std::string debuggerBase;       // "kd", "windbg", or basename from -k
        std::string passthrough;        // trailing debugger options
    };

    [[nodiscard]] bool ArgEquals(const wchar_t* arg, const wchar_t* dash) noexcept
    {
        if (_wcsicmp(arg, dash) == 0)
        {
            return true;
        }
        // The original accepts both the '-' and '/' spelling of every switch.
        if (arg[0] == L'/' && dash[0] == L'-')
        {
            return _wcsicmp(arg + 1, dash + 1) == 0;
        }
        return false;
    }

    // --- Narrow conversion so a console pipe sees the same bytes. ---
    [[nodiscard]] std::string Narrow(const wchar_t* wide)
    {
        if (wide == nullptr || wide[0] == L'\0')
        {
            return {};
        }
        const int needed = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return {};
        }
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_ACP, 0, wide, -1, out.data(), needed, nullptr, nullptr);
        if (!out.empty() && out.back() == '\0')
        {
            out.pop_back();
        }
        return out;
    }

    // --- Banner. Always printed. LiveKd has no -nobanner switch. ---
    void PrintBanner()
    {
        std::fputs(kBannerLine1, stdout);
        std::fputs(kBannerCredit, stdout);
        std::fputs(kBannerAuthor, stdout);
    }

    // --- Usage, transcribed byte-for-byte from sub_14000BBA0. ---
    void PrintUsage()
    {
        static const char* const lines[] = {
            "usage:\n",
            "  livekd [-w|-k debugger-path|-o dumpfile] [-m[flags]] [-mp process|pid] [-vsym] [-b] [debugger options]\n",
            "  livekd [-w|-k debugger-path|-o dumpfile] -ml [-hvd] [-b] [debugger options]\n",
            "  livekd -hvl\n",
            "  livekd [-w|-k debugger-path|-o dumpfile] -hv guid|name [-p] [-vsym] [-b] [debugger options]\n",
            "  livekd [-w|-k debugger-path] -hv guid|name -hvkl [-vsym] [-b] [debugger options]\n",
            "   -hv     Specifies the name or GUID of the Hyper-V VM to debug.\n",
            "   -hvd    Includes hypervisor pages (Windows 8.1 and above only).\n",
            "   -hvl    Lists the names and GUIDs of running Hyper-V VMs.\n",
            "   -k      Specifies complete path and filename of debugger image to execute\n",
            "   -m      Creates a mirror dump, which is a consistent view of kernel memory\n",
            "           Only kernel mode memory will be available, and this option may need\n",
            "           significant amounts of available physical memory.  A flags mask that\n",
            "           specifies which regions to include may optionally be provided (drawn\n",
            "           from the following table, default 0x18F8):\n",
            "\n",
            "           0001 - process private,  0002 - mapped file,   0004 - shared section\n",
            "           0008 - page table pages, 0010 - paged pool,    0020 - non-paged pool\n",
            "           0040 - system PTEs,      0080 - session pages, 0100 - metadata files\n",
            "           0200 - AWE user pages,   0400 - driver pages,  0800 - kernel stacks\n",
            "           1000 - WS metadata,      2000 - large pages\n",
            "\n",
            "           The default captures most kernel memory contents and is recommended.\n",
            "           This option may be used with -o to save faster, consistent dumps.\n",
            "           Mirror dumps require Windows Vista or Windows Server 2008 or above.\n",
            "\n",
            "           Sysinternals RamMap provides a graphical summary of the distribution\n",
            "           of the available memory regions that can be selected for inclusion.\n",
            "   -ml     Generate live dump using native support (Windows 8.1 and above only).\n",
            "   -mp     Specifies a single process whose user mode memory contents should be\n",
            "           included in a mirror dump.  Only effective with the -m option.\n",
            "   -o      Saves a memory.dmp to disk instead of launching the debugger\n",
            "   -p      Pauses the target Hyper-V VM while LiveKd is active\n",
            "           (recommended for use with -o).\n",
            "   -vsym   Displays verbose debugging information about symbol load operations.\n",
            "   -w      Runs windbg instead of kd\n",
            "   -b      Batch mode, don't prompt to execute kd again after completion\n",
            "\n",
            "All other options are passed through to the debugger.\n",
            "Note: Use Ctrl-Break to terminate and restart the debugger if it hangs.\n\n",
            "By default LiveKd runs kd.exe.\n\n",
            nullptr
        };
        for (int i = 0; lines[i] != nullptr; ++i)
        {
            std::fputs(lines[i], stdout);
        }
    }

    // --- EULA gate (sub_1400018B0). Matches the standalone Sysinternals pattern:
    // the suite-wide flag under HKLM or HKCU\Software\Sysinternals, or the
    // per-tool flag under HKCU\Software\Sysinternals\LiveKd. ---
    constexpr wchar_t kSysinternalsKey[] = L"Software\\Sysinternals";
    constexpr wchar_t kLiveKdKey[] = L"Software\\Sysinternals\\LiveKd";

    [[nodiscard]] bool EulaFlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD cb = sizeof(data);
        const LSTATUS st = RegQueryValueExW(key, L"EulaAccepted", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(&data), &cb);
        RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kLiveKdKey, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExW(key, L"EulaAccepted", 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
    }

    // ponytail: the original pops a RichEdit GUI dialog on an interactive desktop
    // (sub_1400018B0). The console message is the faithful headless path for a
    // CLI tool, so the dialog is skipped by design.
    [[nodiscard]] bool EnsureEula(bool acceptFlag)
    {
        if (acceptFlag
            || EulaFlagSet(HKEY_LOCAL_MACHINE, kSysinternalsKey)
            || EulaFlagSet(HKEY_CURRENT_USER, kSysinternalsKey)
            || EulaFlagSet(HKEY_CURRENT_USER, kLiveKdKey))
        {
            WriteEulaAccepted();
            return true;
        }
        std::fputs("\nLiveKd License Agreement\n", stderr);
        std::fputs("This is the first run of this program. "
            "You must accept the EULA to continue.\n", stderr);
        std::fputs("Use -accepteula to accept the EULA.\n\n", stderr);
        return false;
    }

    // --- Elevation check (sub_140008170). CheckTokenMembership against the
    // effective token reports the Administrators group as absent for a
    // non-elevated admin, which is the real "running elevated" test the
    // original's token-group walk performs. ---
    [[nodiscard]] bool IsRunningElevated()
    {
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID adminSid = nullptr;
        if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid))
        {
            return false;
        }
        BOOL isMember = FALSE;
        if (!CheckTokenMembership(nullptr, adminSid, &isMember))
        {
            isMember = FALSE;
        }
        FreeSid(adminSid);
        return isMember != FALSE;
    }

    // --- Enable a named privilege on the process token (sub_140002240). ---
    [[nodiscard]] bool EnablePrivilege(const wchar_t* name)
    {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &rawToken))
        {
            return false;
        }
        ScopedHandle token(rawToken);
        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, name, &luid))
        {
            return false;
        }
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!AdjustTokenPrivileges(token.Get(), FALSE, &tp, sizeof(tp), nullptr, nullptr))
        {
            return false;
        }
        return GetLastError() == ERROR_SUCCESS;
    }

    // --- Native live-dump support needs Windows 8.1 or above. RtlGetVersion is
    // used in place of the VersionHelpers macros because those honour the app
    // manifest's compatibility lie and would misreport the real build
    // (sub_14000BDA0). ---
    [[nodiscard]] bool IsNativeLiveDumpSupported()
    {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return false;
        }
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (rtlGetVersion == nullptr)
        {
            return false;
        }
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        if (rtlGetVersion(&info) < 0)
        {
            return false;
        }
        if (info.dwMajorVersion > 6)
        {
            return true;
        }
        return info.dwMajorVersion == 6 && info.dwMinorVersion >= 3;
    }

    // --- Debugger discovery (sub_14000B7D0 / sub_14000B570). Walk the known
    // install locations, setting each as the current directory, then SearchPath
    // for the debugger image. The registry root under Windows Kits is the modern
    // location. ---
    [[nodiscard]] bool SearchDebugger(const std::string& exeName, std::string& fullPath)
    {
        char buffer[MAX_PATH] = {};
        LPSTR filePart = nullptr;
        const DWORD n = SearchPathA(nullptr, exeName.c_str(), nullptr,
            static_cast<DWORD>(std::size(buffer)), buffer, &filePart);
        if (n == 0 || n >= std::size(buffer))
        {
            return false;
        }
        fullPath = buffer;
        return true;
    }

    void SetDirFromEnv(const char* expand)
    {
        char dst[MAX_PATH] = {};
        const DWORD n = ExpandEnvironmentStringsA(expand, dst, static_cast<DWORD>(std::size(dst)));
        if (n != 0 && n <= std::size(dst))
        {
            SetCurrentDirectoryA(dst);
        }
    }

    [[nodiscard]] bool DiscoverDebugger(const std::string& exeName, std::string& fullPath)
    {
        SetDirFromEnv("%ProgramFiles%\\Debugging Tools for Windows (x64)");
        if (SearchDebugger(exeName, fullPath))
        {
            return true;
        }
        SetDirFromEnv("%ProgramFiles%\\Debugging Tools for Windows 64-bit");
        if (SearchDebugger(exeName, fullPath))
        {
            return true;
        }
        SetCurrentDirectoryA("C:\\debuggers");
        if (SearchDebugger(exeName, fullPath))
        {
            return true;
        }

        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", 0,
            KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS)
        {
            char root[MAX_PATH] = {};
            DWORD cb = sizeof(root);
            DWORD type = 0;
            const LSTATUS st = RegQueryValueExA(key, "WindowsDebuggersRoot", nullptr, &type,
                reinterpret_cast<LPBYTE>(root), &cb);
            RegCloseKey(key);
            if (st == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
            {
                std::string dir = root;
                dir += "\\x64";
                SetCurrentDirectoryA(dir.c_str());
                if (SearchDebugger(exeName, fullPath))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // --- Symbol path setup (the symbol-configuration half of sub_14000AAD0).
    // Verify dbghelp is reachable, then, when _NT_SYMBOL_PATH is not configured,
    // offer to point it at the Microsoft symbol server so the launched debugger
    // can resolve symbols. ---
    [[nodiscard]] bool DbghelpPresent()
    {
        const HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dbghelp == nullptr)
        {
            return false;
        }
        const bool ok = GetProcAddress(dbghelp, "SymInitialize") != nullptr;
        FreeLibrary(dbghelp);
        return ok;
    }

    [[nodiscard]] bool EnsureSymbolPath()
    {
        if (!DbghelpPresent())
        {
            std::fputs("LiveKd requires Dbghelp.dll. Download the Debugging Tools package from\n", stdout);
            std::fputs("www.microsoft.com/whdc/ddk/debugging.\n\n", stdout);
            return false;
        }

        char existing[MAX_PATH] = {};
        if (GetEnvironmentVariableA("_NT_SYMBOL_PATH", existing, static_cast<DWORD>(std::size(existing))) != 0)
        {
            return true;
        }

        std::fputs("\nSymbols are not configured. Would you like LiveKd to set the _NT_SYMBOL_PATH\n", stdout);
        std::fputs("directory to reference the Microsoft symbol server so that symbols can be\n", stdout);
        std::fputs("obtained automatically? (y/n) ", stdout);
        std::fflush(stdout);
        const int answer = std::getchar();
        if ((answer & ~0x20) == 'N')
        {
            std::fputs("\nLivekd will now exit.\n\n", stdout);
            return false;
        }

        std::fputs("\nEnter the folder to which symbols download (default is c:\\symbols): ", stdout);
        std::fflush(stdout);
        char folder[MAX_PATH] = "c:\\Symbols";
        char line[MAX_PATH] = {};
        if (std::fgets(line, static_cast<int>(std::size(line)), stdin) != nullptr)
        {
            line[std::strcspn(line, "\r\n")] = '\0';
            if (line[0] != '\0')
            {
                std::snprintf(folder, std::size(folder), "%s", line);
            }
        }

        char sympath[MAX_PATH * 2] = {};
        std::snprintf(sympath, std::size(sympath),
            "srv*%s*http://msdl.microsoft.com/download/symbols", folder);
        SetEnvironmentVariableA("_NT_SYMBOL_PATH", sympath);

        HKEY env = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &env) == ERROR_SUCCESS)
        {
            RegSetValueExA(env, "_NT_SYMBOL_PATH", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(sympath),
                static_cast<DWORD>(std::strlen(sympath) + 1));
            RegCloseKey(env);
        }
        return true;
    }

    // --- Native live kernel dump (sub_140009830). Resolves NtSystemDebugControl,
    // creates the secured dump file, fills SYSDBG_LIVEDUMP_CONTROL and calls
    // SysDbgGetLiveKernelDump. This is the safe path: it snapshots kernel memory
    // without bugchecking. ---
    [[nodiscard]] bool BuildDumpSecurity(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd)
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

    [[nodiscard]] bool WriteLiveKernelDump(const std::string& path, HANDLE processHandle,
        bool normalFileAttr, bool hypervisorPages, bool useDumpStorageStack)
    {
        if (!EnablePrivilege(L"SeDebugPrivilege"))
        {
            std::fputs("Unable to enable SeDebugPrivilege.  Administrator rights are required to use LiveKd.\n", stdout);
            return false;
        }

        ScopedHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!cancelEvent.Valid())
        {
            std::fprintf(stdout, "CreateEvent failed - error %lu.\n", GetLastError());
            return false;
        }

        const HMODULE ntdll = GetModuleHandleW(L"NTDLL.DLL");
        const auto ntSystemDebugControl = ntdll != nullptr
            ? reinterpret_cast<NtSystemDebugControlFn>(
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

        const DWORD attrs = normalFileAttr
            ? FILE_ATTRIBUTE_NORMAL
            : (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        ScopedHandle dumpFile(CreateFileA(path.c_str(),
            GENERIC_READ | GENERIC_WRITE | DELETE | WRITE_DAC,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            &sa, CREATE_ALWAYS, attrs, nullptr));
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
        const LONG status = ntSystemDebugControl(kSysDbgGetLiveKernelDump,
            &control, sizeof(control), nullptr, 0, nullptr);
        if (status >= 0)
        {
            std::fputs(" done.\n", stdout);
            return true;
        }
        std::fprintf(stdout, " failed (status %08X).\n", static_cast<unsigned int>(status));
        return false;
    }

    // --- Process resolution for -mp (sub_14000B9D0). Accepts a PID or an image
    // name. ---
    [[nodiscard]] bool AllDigits(const std::string& s) noexcept
    {
        if (s.empty())
        {
            return false;
        }
        for (const char c : s)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] HANDLE OpenTargetProcess(const std::string& token)
    {
        DWORD pid = 0;
        if (AllDigits(token))
        {
            pid = static_cast<DWORD>(std::strtoul(token.c_str(), nullptr, 10));
        }
        else
        {
            ScopedHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (snap.Valid())
            {
                PROCESSENTRY32W entry{};
                entry.dwSize = sizeof(entry);
                if (Process32FirstW(snap.Get(), &entry))
                {
                    const std::string wantExe = token + ".exe";
                    do
                    {
                        const std::string name = Narrow(entry.szExeFile);
                        if (_stricmp(name.c_str(), token.c_str()) == 0
                            || _stricmp(name.c_str(), wantExe.c_str()) == 0)
                        {
                            pid = entry.th32ProcessID;
                            break;
                        }
                    } while (Process32NextW(snap.Get(), &entry));
                }
            }
        }
        if (pid == 0)
        {
            return nullptr;
        }
        return OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }

    // --- Launch the debugger against a freshly written live dump, reproducing
    // the original's re-run loop (main). The live view is approximated by a
    // consistent kernel snapshot, which is the safe equivalent of the legacy
    // driver's live memory window. ---
    [[nodiscard]] int LaunchDebugger(const Config& cfg, const std::string& debuggerFull,
        HANDLE processHandle)
    {
        char windir[MAX_PATH] = {};
        GetWindowsDirectoryA(windir, static_cast<DWORD>(std::size(windir)));
        const std::string dumpPath = std::string(windir) + "\\" + kDefaultDumpName;

        // "Dumpchk" wants -e, every other debugger wants -z to open the dump.
        const char* openSwitch = _stricmp(cfg.debuggerBase.c_str(), "Dumpchk") == 0 ? " -e " : " -z ";
        const std::string exeToken = cfg.useWindbg ? "windbg.exe " : "kd.exe ";
        const std::string commandLine = exeToken + cfg.passthrough + openSwitch + dumpPath;

        for (;;)
        {
            if (!WriteLiveKernelDump(dumpPath, processHandle, false,
                cfg.hypervisorPages, cfg.dumpStorageStack))
            {
                return kExitError;
            }

            std::fprintf(stdout, "Launching %s:\n", debuggerFull.c_str());
            std::string mutableCmd = commandLine;
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (!CreateProcessA(debuggerFull.c_str(), mutableCmd.data(), nullptr, nullptr,
                TRUE, 0, nullptr, nullptr, &si, &pi))
            {
                const DWORD err = GetLastError();
                std::fprintf(stdout, "Error launching %s:\n", cfg.debuggerBase.c_str());
                std::fprintf(stdout, "Error %lu\n", err);
                DeleteFileA(dumpPath.c_str());
                return kExitError;
            }
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            DeleteFileA(dumpPath.c_str());

            if (cfg.batch)
            {
                break;
            }
            std::fprintf(stdout, "\nExecute %s again? (y/n) ", cfg.debuggerBase.c_str());
            std::fflush(stdout);
            char line[MAX_PATH] = {};
            if (std::fgets(line, static_cast<int>(std::size(line)), stdin) == nullptr)
            {
                break;
            }
            if (std::strchr(line, 'y') == nullptr && std::strchr(line, 'Y') == nullptr)
            {
                break;
            }
        }
        std::fputs("\nExiting LiveKd.\n\n", stdout);
        return 0;
    }

    // --- Argument parsing (main). Switch values follow their switch, unknown
    // tokens pass through to the debugger. ---
    [[nodiscard]] bool ParseArgs(int argc, wchar_t** argv, Config& cfg, HANDLE& processHandle,
        bool& wantUsage)
    {
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* a = argv[i];
            if (ArgEquals(a, L"-?"))
            {
                wantUsage = true;
                return true;
            }
            else if (ArgEquals(a, L"-accepteula"))
            {
                // Accepted before parsing. No effect here.
                continue;
            }
            else if (ArgEquals(a, L"-w"))
            {
                cfg.useWindbg = true;
                cfg.debuggerBase = "windbg";
            }
            else if (ArgEquals(a, L"-b"))
            {
                cfg.batch = true;
            }
            else if (ArgEquals(a, L"-vsym"))
            {
                cfg.verboseSymbols = true;
            }
            else if (ArgEquals(a, L"-ml"))
            {
                cfg.nativeLive = true;
            }
            else if (ArgEquals(a, L"-hvd"))
            {
                cfg.hypervisorPages = true;
            }
            else if (ArgEquals(a, L"-md"))
            {
                cfg.dumpStorageStack = true;
            }
            else if (ArgEquals(a, L"-o"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                char full[MAX_PATH] = {};
                const std::string arg = Narrow(argv[i]);
                if (GetFullPathNameA(arg.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr) != 0)
                {
                    cfg.dumpFile = full;
                }
                else
                {
                    cfg.dumpFile = arg;
                }
                cfg.dumpToFile = true;
            }
            else if (ArgEquals(a, L"-k"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                cfg.debuggerPath = Narrow(argv[i]);
                const std::string::size_type slash = cfg.debuggerPath.find_last_of("\\/");
                cfg.debuggerBase = (slash == std::string::npos)
                    ? cfg.debuggerPath
                    : cfg.debuggerPath.substr(slash + 1);
            }
            else if (ArgEquals(a, L"-mp"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                const std::string token = Narrow(argv[i]);
                processHandle = OpenTargetProcess(token);
                if (processHandle == nullptr)
                {
                    std::fprintf(stdout, "Unable to open process '%s'.\n", token.c_str());
                    return false;
                }
            }
            else if (ArgEquals(a, L"-y"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                SetEnvironmentVariableA("_NT_SYMBOL_PATH", Narrow(argv[i]).c_str());
            }
            else
            {
                // Pass every other token through to the debugger command line.
                if (!cfg.passthrough.empty())
                {
                    cfg.passthrough += ' ';
                }
                cfg.passthrough += Narrow(a);
            }
        }
        return true;
    }
}

int wmain(int argc, wchar_t** argv)
{
    PrintBanner();

    bool acceptEula = false;
    for (int i = 1; i < argc; ++i)
    {
        if (ArgEquals(argv[i], L"-accepteula"))
        {
            acceptEula = true;
            break;
        }
    }
    if (!EnsureEula(acceptEula))
    {
        return 1;
    }

    if (!IsRunningElevated())
    {
        std::fputs("Livekd requires administrative rights. You must run it from an\n", stdout);
        std::fputs("elevated command prompt.\n\n", stdout);
        return kExitError;
    }

    if (!EnablePrivilege(L"SeDebugPrivilege"))
    {
        std::fputs("Unable to enable SeDebugPrivilege. You must run LiveKd from an elevated command prompt.\n", stdout);
        return kExitError;
    }

    Config cfg;
    cfg.debuggerBase = "kd";
    ScopedHandle processHandle;
    HANDLE rawProcess = nullptr;
    bool wantUsage = false;
    if (!ParseArgs(argc, argv, cfg, rawProcess, wantUsage))
    {
        return kExitError;
    }
    processHandle.Reset(rawProcess);
    if (wantUsage)
    {
        PrintUsage();
        return kExitError;
    }

    // This recreation reproduces only the native live-dump path, which needs
    // Windows 8.1 or above.
    if (!IsNativeLiveDumpSupported())
    {
        std::fputs("This LiveKd recreation uses the native live-dump API, which requires\n", stdout);
        std::fputs("Windows 8.1 or above.\n\n", stdout);
        return kExitError;
    }

    // -o path: write the live kernel dump and exit without launching a debugger.
    if (cfg.dumpToFile)
    {
        const bool ok = WriteLiveKernelDump(cfg.dumpFile, processHandle.Get(), true,
            cfg.hypervisorPages, cfg.dumpStorageStack);
        if (!ok)
        {
            std::fprintf(stdout, "Error saving dump %s:\n", cfg.dumpFile.c_str());
            return kExitError;
        }
        return 0;
    }

    // Launch path: resolve the debugger, make sure symbols are configured, then
    // run it against a consistent live snapshot.
    std::string debuggerFull;
    if (!cfg.debuggerPath.empty())
    {
        debuggerFull = cfg.debuggerPath;
        if (debuggerFull.find('\\') == std::string::npos)
        {
            std::fputs("A fully qualified debugger path must be supplied.\n", stdout);
            return kExitError;
        }
    }
    else
    {
        const std::string exeName = cfg.useWindbg ? "windbg.exe" : "kd.exe";
        if (!DiscoverDebugger(exeName, debuggerFull))
        {
            std::fprintf(stdout, "Error finding %s:\n", exeName.c_str());
            std::fputs("Use -k <path to 64-bit debugger directory>\\kd.exe\n", stdout);
            return kExitError;
        }
    }

    if (!EnsureSymbolPath())
    {
        return kExitError;
    }

    return LaunchDebugger(cfg, debuggerFull, processHandle.Get());
}
