// PsTools.h -- the shared Sysinternals PsTools command-line framework.
//
// Recovered from PsGetsid64.exe (the first PsTools binary reconstructed) and
// factored so the rest of the PsTools suite reuses it. Every PsTools utility
// shares the same front end:
//
//   [\\computer[,computer[,...]] | @file] [-u user [-p password]] [tool args]
//   plus the -accepteula and -nobanner switches and the startup banner.
//
// This header owns only the generic mechanics. A tool supplies its own banner
// fields, its usage text, the SID or service specifics, and the per-computer
// worker callback. The parsing, banner, EULA gate, target iteration, alternate
// credentials, and error reporting all live here.
//
// Source map (PsGetsid64.exe):
//   ParseCommandLine     sub_140005770
//   ForEachComputer      sub_140003190
//   Banner printer       sub_140002C90 / sub_1400029E0
//   EULA check / accept  sub_140001CC0 / sub_140001C20 / sub_140002300
//   -accepteula strip    sub_140001B90 / sub_1400020E0
//   RemoteConnect        sub_1400040F0 (WNetAddConnection2A to \\computer\IPC$)
//   RemoteDisconnect     sub_1400041C0 (WNetCancelConnection2A)
//   PrintSystemError     sub_140003800 (FormatMessage, netmsg.dll for net codes)
//   LocalComputerName    sub_140001500 (GetComputerNameA)
//   PromptHiddenPassword sub_140001670 (_getch loop)
#pragma once

#include <windows.h>
#include <lm.h>
#include <winnetwk.h>

#include <conio.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace pstools
{
    // --- Banner ------------------------------------------------------------

    struct Banner
    {
        const char* name = "";         // version-resource InternalName
        const char* version = "";      // FileVersion, without a leading 'v'
        const char* description = "";  // FileDescription
        const char* copyright = "";    // LegalCopyright
        const char* company = "";      // CompanyName
    };

    // The banner goes to stdout when stdout is a pipe, otherwise to stderr, so a
    // redirected-to-file run keeps stdout clean for the tool's own output. This
    // matches the original's stream selection (sub_140001E00 tests FILE_TYPE_PIPE).
    [[nodiscard]] inline bool StdoutIsPipe()
    {
        const HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
        return ::GetFileType(h) == FILE_TYPE_PIPE;
    }

    inline void PrintBanner(const Banner& b, bool noBanner)
    {
        if (noBanner)
        {
            return;
        }
        std::FILE* const out = StdoutIsPipe() ? stdout : stderr;
        std::fprintf(out, "\n%s v%s - %s\n%s\n%s\n\n",
                     b.name, b.version, b.description, b.copyright, b.company);
        std::fflush(out);
    }

    // Remove the first argv entry that case-insensitively equals one of two
    // spellings (for example "-nobanner" / "/nobanner"), shifting the rest down.
    // Returns true when a match was removed. The original scans and shifts argv
    // in place for both -nobanner and -accepteula.
    inline bool StripFlag(int& argc, char** argv, const char* dashForm, const char* slashForm)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (::_stricmp(argv[i], dashForm) == 0 || ::_stricmp(argv[i], slashForm) == 0)
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

    // --- EULA --------------------------------------------------------------

    inline constexpr char kSysinternalsKey[] = "Software\\Sysinternals";
    inline constexpr char kEulaValue[] = "EulaAccepted";

    namespace detail
    {
        // Read a nonzero DWORD "EulaAccepted" under root\sub (64-bit view).
        [[nodiscard]] inline bool EulaFlagSet(HKEY root, const std::string& sub)
        {
            HKEY key = nullptr;
            if (::RegOpenKeyExA(root, sub.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key)
                != ERROR_SUCCESS)
            {
                return false;
            }
            DWORD data = 0;
            DWORD cb = sizeof(data);
            const LSTATUS st = ::RegQueryValueExA(key, kEulaValue, nullptr, nullptr,
                                                  reinterpret_cast<LPBYTE>(&data), &cb);
            ::RegCloseKey(key);
            return st == ERROR_SUCCESS && data != 0;
        }
    }

    // The EULA counts as accepted when the suite-wide flag is set under HKLM or
    // HKCU\Software\Sysinternals, or the per-tool flag under
    // HKCU\Software\Sysinternals\<app>. Mirrors sub_140001CC0.
    [[nodiscard]] inline bool IsEulaAccepted(const char* app)
    {
        if (detail::EulaFlagSet(HKEY_LOCAL_MACHINE, kSysinternalsKey)
            || detail::EulaFlagSet(HKEY_CURRENT_USER, kSysinternalsKey))
        {
            return true;
        }
        const std::string sub = std::string(kSysinternalsKey) + "\\" + app;
        return detail::EulaFlagSet(HKEY_CURRENT_USER, sub);
    }

    // Persist acceptance under HKCU\Software\Sysinternals\<app>\EulaAccepted = 1.
    inline void WriteEulaAccepted(const char* app)
    {
        const std::string sub = std::string(kSysinternalsKey) + "\\" + app;
        HKEY key = nullptr;
        if (::RegCreateKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
                              KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            ::RegSetValueExA(key, kEulaValue, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&one), sizeof(one));
            ::RegCloseKey(key);
        }
    }

    // Gate the run on the EULA. Returns true to proceed. When -accepteula was
    // passed or the flag is already stored, it records acceptance and returns.
    // Otherwise it prompts once on the console (the headless Y/N path the
    // original falls back to on Nano and redirected hosts).
    //
    // ponytail: the original also shows a RichEdit GUI dialog on interactive
    // desktops (sub_140002300). The console Y/N prompt is the faithful headless
    // path a console tool needs, so the GUI dialog is skipped by design.
    [[nodiscard]] inline bool AcceptEula(const char* app, bool acceptFlag)
    {
        if (acceptFlag || IsEulaAccepted(app))
        {
            WriteEulaAccepted(app);
            return true;
        }
        int ch = 0;
        for (;;)
        {
            std::printf("Accept Eula (Y/N)?");
            std::fflush(stdout);
            ch = ::_getch();
            std::printf("%c\n", ch);
            if ((ch & ~0x20) == 'Y')
            {
                WriteEulaAccepted(app);
                return true;
            }
            if ((ch & ~0x20) == 'N')
            {
                return false;
            }
        }
    }

    // --- Error reporting ---------------------------------------------------

    inline constexpr DWORD kNetErrLow = 2100;   // LANMAN error range start
    inline constexpr DWORD kNetErrHigh = 2483;  // 2100 + 0x383

    // Print a Windows error code as text to stderr. Network errors (2100..2483)
    // resolve against netmsg.dll. Mirrors sub_140003800.
    inline void PrintSystemError(DWORD code)
    {
        HMODULE netmsg = nullptr;
        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM;
        if (code >= kNetErrLow && code <= kNetErrHigh)
        {
            netmsg = ::LoadLibraryExA("netmsg.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
            if (netmsg != nullptr)
            {
                flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                        | FORMAT_MESSAGE_FROM_HMODULE;
            }
        }
        LPSTR buffer = nullptr;
        const DWORD n = ::FormatMessageA(flags, netmsg, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                         reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
        if (n != 0 && buffer != nullptr)
        {
            DWORD written = 0;
            ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), buffer, n, &written, nullptr);
            ::LocalFree(buffer);
        }
        if (netmsg != nullptr)
        {
            ::FreeLibrary(netmsg);
        }
    }

    // --- Local identity ----------------------------------------------------

    [[nodiscard]] inline std::string LocalComputerName()
    {
        char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD cch = static_cast<DWORD>(std::size(name));
        if (!::GetComputerNameA(name, &cch))
        {
            return {};
        }
        return std::string(name, cch);
    }

    [[nodiscard]] inline std::string PromptHiddenPassword()
    {
        std::printf("Password: ");
        std::fflush(stdout);
        std::string pw;
        for (int ch = ::_getch(); ch != '\r'; ch = ::_getch())
        {
            pw.push_back(static_cast<char>(ch));
        }
        std::printf("\n");
        return pw;
    }

    // --- Alternate credentials over the IPC$ share -------------------------

    namespace detail
    {
        inline bool g_connected = false;  // a credentialed IPC$ session is open
    }

    // Open an authenticated session to \\computer\IPC$ with the supplied
    // credentials. Mirrors sub_1400040F0.
    [[nodiscard]] inline bool RemoteConnect(const char* computer, const char* user, const char* password)
    {
        if (user == nullptr || user[0] == '\0')
        {
            return false;
        }
        char remote[MAX_PATH] = {};
        std::snprintf(remote, std::size(remote), "\\\\%s\\IPC$", computer);
        NETRESOURCEA nr = {};
        nr.dwType = RESOURCETYPE_ANY;
        nr.lpRemoteName = remote;
        const DWORD rc = ::WNetAddConnection2A(&nr, password, user, 0);
        if (rc == NO_ERROR)
        {
            detail::g_connected = true;
            return true;
        }
        ::SetLastError(rc);
        return false;
    }

    inline void RemoteDisconnect(const char* computer)
    {
        if (!detail::g_connected)
        {
            return;
        }
        char remote[MAX_PATH] = {};
        std::snprintf(remote, std::size(remote), "\\\\%s\\IPC$", computer);
        ::WNetCancelConnection2A(remote, 0, TRUE);
    }

    // --- Command line ------------------------------------------------------

    struct Targets
    {
        std::string computerList;       // \\computer, comma list, "@file", or "*"
        std::string account;            // trailing account name or SID string
        std::string user;               // -u value
        std::string password;           // -p value (or prompted)
        bool computerSpecified = false;
        bool accountSpecified = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    // Parse the shared front-end arguments (after -nobanner and -accepteula have
    // been stripped). Returns false when the caller should print usage. Mirrors
    // sub_140005770: a leading '-' or '/' marks a switch, '@' names a response
    // file, '\\' prefixes a computer, anything else is the account/SID. -u needs
    // a computer target and -p needs -u.
    [[nodiscard]] inline bool ParseCommandLine(int argc, char** argv, Targets& t)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];
            if (a[0] == '-' || a[0] == '/')
            {
                if (a[1] == '\0')
                {
                    continue;  // a bare '-' is ignored, as in the original
                }
                const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[1])));
                if (c == 'u')
                {
                    if (i + 1 >= argc || t.haveUser)
                    {
                        return false;
                    }
                    t.user = argv[++i];
                    t.haveUser = true;
                }
                else if (c == 'p')
                {
                    if (i + 1 >= argc || t.havePassword)
                    {
                        return false;
                    }
                    t.password = argv[++i];
                    t.havePassword = true;
                }
                else
                {
                    return false;
                }
            }
            else if (a[0] == '@')
            {
                t.computerList = a;  // keep the '@' so ForEachComputer sees it
                t.computerSpecified = true;
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (t.computerSpecified)
                {
                    return false;
                }
                t.computerList = a + 2;
                t.computerSpecified = true;
            }
            else
            {
                if (t.accountSpecified)
                {
                    return false;
                }
                t.account = a;
                t.accountSpecified = true;
            }
        }

        if ((t.haveUser && !t.computerSpecified) || (t.havePassword && !t.haveUser))
        {
            return false;
        }
        if (t.computerList.empty())
        {
            t.computerList = LocalComputerName();
        }
        return true;
    }

    // --- Target iteration --------------------------------------------------

    using ComputerFn = std::function<bool(const char* computer)>;

    // Run fn once per target named by the list. Mirrors sub_140003190:
    //   "@file"   -- one computer per non-empty line
    //   "*"       -- every server the domain enumerator returns
    //   "a,b,c"   -- a comma-separated list
    //   "host"    -- a single computer, called directly with no header
    // For the file, domain, and comma forms a "\\computer:" header is printed
    // before each when showHeaders is set. Returns true when every target
    // succeeded.
    //
    // ponytail: the original's multi-target return folds to "any succeeded". This
    // returns "all succeeded", which is saner and identical on the single-target
    // path that the local cases exercise.
    inline bool ForEachComputer(const std::string& list, bool showHeaders, const ComputerFn& fn)
    {
        if (!list.empty() && list[0] == '@')
        {
            const char* path = list.c_str() + 1;
            std::FILE* f = nullptr;
            if (::fopen_s(&f, path, "r") != 0 || f == nullptr)
            {
                std::fprintf(stderr, "\rError opening %s:\n", path);
                PrintSystemError(::GetLastError());
                return true;
            }
            bool ok = true;
            char line[260] = {};
            while (std::fgets(line, static_cast<int>(std::size(line)), f) != nullptr)
            {
                line[std::strcspn(line, "\n")] = '\0';
                if (line[0] != '\0')
                {
                    if (showHeaders)
                    {
                        std::printf("\\\\%s:\n", line);
                    }
                    if (!fn(line))
                    {
                        ok = false;
                    }
                }
            }
            std::fclose(f);
            return ok;
        }

        if (list == "*")
        {
            std::fprintf(stderr, "Enumerating domain...\n");
            LPBYTE buffer = nullptr;
            DWORD read = 0;
            DWORD total = 0;
            DWORD resume = 0;
            const NET_API_STATUS st = ::NetServerEnum(nullptr, 101, &buffer, MAX_PREFERRED_LENGTH,
                                                      &read, &total, SV_TYPE_ALL, nullptr, &resume);
            if (st != NERR_Success && st != ERROR_MORE_DATA)
            {
                std::fprintf(stderr, "A system error has occurred: %lu\n", static_cast<unsigned long>(st));
                return true;
            }
            bool ok = true;
            auto* info = reinterpret_cast<SERVER_INFO_101*>(buffer);
            for (DWORD i = 0; info != nullptr && i < read; ++i)
            {
                char name[260] = {};
                ::WideCharToMultiByte(CP_ACP, 0, info[i].sv101_name, -1, name,
                                      static_cast<int>(std::size(name)), nullptr, nullptr);
                if (showHeaders)
                {
                    std::printf("\\\\%s:\n", name);
                }
                if (!fn(name))
                {
                    ok = false;
                }
            }
            if (buffer != nullptr)
            {
                ::NetApiBufferFree(buffer);
            }
            return ok;
        }

        if (list.find(',') == std::string::npos)
        {
            return fn(list.c_str());
        }

        bool ok = true;
        std::string work = list;
        size_t start = 0;
        while (start <= work.size())
        {
            const size_t comma = work.find(',', start);
            const std::string one = work.substr(start, comma == std::string::npos ? std::string::npos
                                                                                  : comma - start);
            if (!one.empty())
            {
                if (showHeaders)
                {
                    std::printf("\\\\%s:\n", one.c_str());
                }
                if (!fn(one.c_str()))
                {
                    ok = false;
                }
            }
            if (comma == std::string::npos)
            {
                break;
            }
            start = comma + 1;
        }
        return ok;
    }
}
