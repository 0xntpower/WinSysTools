// PsFile -- lists or closes the files opened on the local or a remote computer
// through a network share. Source-accurate C++20 console reconstruction of the
// Sysinternals psfile64.exe v1.04.
//
// Behaviour recovered with IDA (idalib) from psfile64.exe:
//   main                 sub_140004560
//   front end driver     sub_140003090  (version info, banner, EULA, arg strip)
//   banner + nobanner    sub_140002980
//   EULA gate            sub_140002080 / sub_1400022A0
//   command parser       sub_140003F50  (\\computer, -u, -p, Id | path, -c)
//   usage                sub_140004440
//   worker               sub_1400039E0  (NetFileEnum / NetFileGetInfo / NetFileClose)
//   per-file record      sub_140003EC0  ([id] path, user, locks, access)
//   close one by id      sub_140003960  (NetFileClose + "Closed file")
//   remote connect       sub_140003290  (WNetAddConnection2 to \\computer\IPC$)
//   remote disconnect    sub_140003350
//   local computer name  sub_1400013A0  (GetComputerNameW)
//   hidden password      sub_140001520
//   print a system error sub_1400031C0  (FormatMessage, netmsg.dll for net codes)
//
// The shared PsTools front end (banner, EULA gate, alternate credentials, error
// messages, local identity, hidden password prompt) lives in ../common/PsTools.h and
// is reused read only. PsFile has its own argument grammar, so the parser here
// mirrors sub_140003F50 rather than pstools::ParseCommandLine. The share-file
// enumeration, the record format, and the ANSI/wide conversion for the Unicode
// only Net* share APIs are not in the shared header and are implemented locally.
//
// The Net* share-management APIs are wide only, so the computer and path are
// converted to UTF-16 at the call and the returned path and user names back to
// the active code page for printing, matching the ANSI front end of the suite.

#include "../common/PsTools.h"

#include <windows.h>
#include <lm.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    // Version-resource fields of psfile64.exe v1.04, used for the banner and the
    // EULA registry key. The original reads these from its own version resource
    // at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsFile",
        "1.04",
        "Lists files and directories opened remotely",
        "Independent source-accurate recreation of PsFile by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key is
    // case-insensitive, so this matches HKCU\Software\Sysinternals\PsFile.
    constexpr char kApp[] = "PsFile";

    // FILE_INFO_3 fi3_permissions bits (lmaccess.h PERM_FILE_*), named here so the
    // access line stays free of magic numbers.
    constexpr DWORD kPermRead = 0x1;
    constexpr DWORD kPermWrite = 0x2;
    constexpr DWORD kPermCreate = 0x4;

    // --- Parsed command line -----------------------------------------------

    struct Options
    {
        std::string computer;   // \\computer, stored without the leading slashes
        std::string path;       // partial path filter passed as the Net qualifier
        std::string user;       // -u value
        std::string password;   // -p value or prompted
        DWORD fileId = 0;       // file id when haveId
        bool haveId = false;
        bool havePath = false;
        bool closeFile = false;         // -c
        bool computerSpecified = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    void PrintUsage(const char* exe)
    {
        std::printf("PsFile lists or closes files opened remotely.\n");
        std::printf("\nUsage: %s [\\\\RemoteComputer [-u Username [-p Password]]] [[Id | path] [-c]]\n", exe);
        std::printf("     -u        Specifies optional user name for login to\n"
                    "               remote computer.\n");
        std::printf("     -p        Specifies password for user name.\n");
        std::printf("     Id        Id of file to print information for or close.\n");
        std::printf("     Path      Full or partial path of files to match.\n");
        std::printf("     -c        Closes file identified by file Id.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("Omitting a file identifier has PsFile list all files opened remotely.\n");
        std::printf("\n");
    }

    // --- ANSI / wide conversion for the Unicode-only Net* share APIs --------

    [[nodiscard]] std::wstring Widen(const std::string& s)
    {
        if (s.empty())
        {
            return {};
        }
        const int n = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 0)
        {
            return {};
        }
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
        return w;
    }

    [[nodiscard]] std::string Narrow(const wchar_t* s)
    {
        if (s == nullptr || *s == L'\0')
        {
            return {};
        }
        const int n = ::WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0)
        {
            return {};
        }
        std::string a(static_cast<size_t>(n - 1), '\0');
        ::WideCharToMultiByte(CP_ACP, 0, s, -1, a.data(), n, nullptr, nullptr);
        return a;
    }

    // --- Command line ------------------------------------------------------

    // Parse PsFile's own argument grammar, after -nobanner and -accepteula have
    // been stripped. Returns false when the caller should print usage. Mirrors
    // sub_140003F50: a leading '-' or '/' is a switch (-u value, -p value, -c), a
    // '\\' prefixes a computer, a token that starts with a digit is a file id,
    // anything else is a partial path. -c is accepted only after an id or path has
    // already been seen, -u needs a computer target, and -p needs -u.
    [[nodiscard]] bool Parse(int argc, char** argv, Options& o)
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
                for (int j = 1; a[j] != '\0'; ++j)
                {
                    const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[j])));
                    if (c == 'c')
                    {
                        if (!o.haveId && !o.havePath)
                        {
                            return false;  // -c before any id or path
                        }
                        o.closeFile = true;
                        continue;
                    }
                    if (c == 'u')
                    {
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        break;
                    }
                    if (c == 'p')
                    {
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        break;
                    }
                    return false;
                }
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (o.computerSpecified)
                {
                    return false;
                }
                o.computer = a + 2;
                o.computerSpecified = true;
            }
            else
            {
                if (o.haveId || o.havePath)
                {
                    return false;
                }
                if (::isdigit(static_cast<unsigned char>(a[0])) != 0)
                {
                    unsigned long value = 0;
                    const char* const end = a + std::strlen(a);
                    const auto [ptr, ec] = std::from_chars(a, end, value);
                    if (ptr == a || ec == std::errc::result_out_of_range)
                    {
                        return false;
                    }
                    o.fileId = static_cast<DWORD>(value);
                    o.haveId = true;
                }
                else
                {
                    o.path = a;
                    o.havePath = true;
                }
            }
        }

        if ((o.haveUser && !o.computerSpecified) || (o.havePassword && !o.haveUser))
        {
            return false;
        }
        return true;
    }

    // --- Share file enumeration --------------------------------------------

    // ponytail: the original leaks the NetApi buffer (the process exits straight
    // after), this frees it. Output is unchanged.
    struct NetApiBuf
    {
        LPBYTE ptr = nullptr;

        explicit NetApiBuf(LPBYTE p)
            : ptr(p)
        {
        }

        ~NetApiBuf()
        {
            if (ptr != nullptr)
            {
                ::NetApiBufferFree(ptr);
            }
        }

        NetApiBuf(const NetApiBuf&) = delete;
        NetApiBuf& operator=(const NetApiBuf&) = delete;
    };

    // Print one open-file record. Mirrors sub_140003EC0.
    void PrintRecord(const FILE_INFO_3& fi)
    {
        std::printf("[%u] %s\n", fi.fi3_id, Narrow(fi.fi3_pathname).c_str());
        std::printf("    User:   %s\n", Narrow(fi.fi3_username).c_str());
        std::printf("    Locks:  %d\n", fi.fi3_num_locks);
        std::printf("    Access: ");
        if ((fi.fi3_permissions & kPermRead) != 0)
        {
            std::printf("Read ");
        }
        if ((fi.fi3_permissions & kPermWrite) != 0)
        {
            std::printf("Write ");
        }
        if ((fi.fi3_permissions & kPermCreate) != 0)
        {
            std::printf("Create");
        }
        std::printf("\n");
    }

    // Enumerate, list, or close the remotely opened files. Mirrors sub_1400039E0.
    // Returns NERR_Success (0) on success, otherwise the Net* status the caller
    // reports. displayName is the computer name shown in messages: the typed name
    // when a computer was given, the local name otherwise.
    [[nodiscard]] DWORD RunWorker(const Options& o, const std::string& displayName)
    {
        std::wstring serverW;
        LPWSTR server = nullptr;  // Net* share APIs take LMSTR (non-const), input only
        if (o.computerSpecified)
        {
            serverW = L"\\\\" + Widen(o.computer);
            server = serverW.data();
        }

        if (!o.haveId)
        {
            std::wstring pathW;
            LPWSTR basepath = nullptr;
            if (o.havePath)
            {
                pathW = Widen(o.path);
                basepath = pathW.data();
            }

            LPBYTE buf = nullptr;
            DWORD read = 0;
            DWORD total = 0;
            const NET_API_STATUS st = ::NetFileEnum(server, basepath, nullptr, 3, &buf,
                                                    MAX_PREFERRED_LENGTH, &read, &total, nullptr);
            if (st != NERR_Success)
            {
                return st;
            }
            const NetApiBuf guard(buf);
            const auto* files = reinterpret_cast<const FILE_INFO_3*>(buf);

            if (read == 0)
            {
                std::printf("No files opened remotely on %s.\n", displayName.c_str());
                return NERR_Success;
            }

            if (!o.closeFile)
            {
                if (o.havePath)
                {
                    std::printf("Files opened remotely on %s matching %s:\n\n",
                                displayName.c_str(), o.path.c_str());
                }
                else
                {
                    std::printf("Files opened remotely on %s:\n\n", displayName.c_str());
                }
                for (DWORD i = 0; i < read; ++i)
                {
                    PrintRecord(files[i]);
                }
                return NERR_Success;
            }

            NET_API_STATUS last = NERR_Success;
            for (DWORD i = 0; i < read; ++i)
            {
                last = ::NetFileClose(server, files[i].fi3_id);
                if (last == NERR_Success)
                {
                    std::printf("Closed file %s on %s.\n",
                                Narrow(files[i].fi3_pathname).c_str(), displayName.c_str());
                }
            }
            return last;
        }

        LPBYTE buf = nullptr;
        const NET_API_STATUS st = ::NetFileGetInfo(server, o.fileId, 3, &buf);
        if (st != NERR_Success)
        {
            return st;
        }
        const NetApiBuf guard(buf);
        const auto* fi = reinterpret_cast<const FILE_INFO_3*>(buf);

        if (o.closeFile)
        {
            const NET_API_STATUS cst = ::NetFileClose(server, fi->fi3_id);
            if (cst == NERR_Success)
            {
                std::printf("Closed file %s on %s.\n", Narrow(fi->fi3_pathname).c_str(),
                            displayName.c_str());
            }
            return cst;
        }

        std::printf("File %d on %s:\n\n", static_cast<int>(o.fileId), displayName.c_str());
        PrintRecord(*fi);
        return NERR_Success;
    }
}

int main(int argc, char** argv)
{
    const char* const exe = (argc > 0) ? argv[0] : "psfile";

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    Options o;
    if (!Parse(argc, argv, o))
    {
        PrintUsage(exe);
        return -1;
    }

    // -u without -p prompts for a hidden password, as the original does.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    // Messages always name a computer: the typed name, or the local name for a
    // local run. The Net* calls get \\computer when a computer was typed, NULL
    // (the local machine) otherwise. A typed name that resolves to this machine
    // takes the local path, the same test main makes before the connect retry.
    const std::string localName = pstools::LocalComputerName();
    const std::string displayName = o.computerSpecified ? o.computer : localName;
    const bool isRemote = o.computerSpecified
                          && ::_stricmp(o.computer.c_str(), localName.c_str()) != 0;

    DWORD status = NERR_Success;
    if (isRemote)
    {
        status = RunWorker(o, displayName);
        if (status == NERR_Success)
        {
            return 0;
        }
        // The first attempt runs without credentials. Only on failure does the
        // original authenticate to \\computer\IPC$ and retry. RemoteConnect fails
        // immediately when no user was given, which prints the connect error and
        // then reports the original worker status.
        if (!pstools::RemoteConnect(o.computer.c_str(), o.user.c_str(), o.password.c_str()))
        {
            std::printf("Unable to connect to %s. Verify user name and password.\n",
                        o.computer.c_str());
        }
        else
        {
            status = RunWorker(o, displayName);
            pstools::RemoteDisconnect(o.computer.c_str());
        }
    }
    else
    {
        status = RunWorker(o, displayName);
    }

    if (status != NERR_Success)
    {
        if (!o.haveId)
        {
            std::printf("Error listing remotely open files on %s:\n", displayName.c_str());
        }
        else
        {
            std::printf("Error closing file %d on %s:\n", static_cast<int>(o.fileId),
                        displayName.c_str());
        }
        pstools::PrintSystemError(status);
    }
    return 0;
}
