// Strings - console recreation of the Sysinternals Strings utility (v2.54).
//
// Scans files, wildcards, or directories for runs of printable ASCII and
// little-endian Unicode characters at least a minimum length long and prints
// them one per line. Reverse engineered from targets/sysinternals/strings64.exe
// (main at 0x140003EF0, core scan at 0x1400037E0, walker at 0x1400033B0,
// EULA at 0x140002400). Scan logic and output format are reproduced so the
// extracted strings match the original byte for byte.

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mbstring.h>
#include <string>

namespace
{
    // One file read is capped at this many bytes, matching the original. The
    // buffer holds one extra window so the two-byte Unicode probe at the final
    // index never reads outside the allocation.
    constexpr DWORD kChunkSize = 0xFFFF;
    constexpr int kBufferSize = 0x10000;

    // A printable ASCII byte sits in the inclusive range 0x20 to 0x7E. The
    // original expresses the complement as (unsigned char)(c - 32) > 0x5E.
    constexpr int kAsciiLow = 0x20;
    constexpr int kPrintableSpan = 0x5E;

    // How many files are opened between progress dots on a large scan.
    constexpr int kProgressInterval = 500;
    constexpr int kProgressDots = 3;

    constexpr const char* kName = "Strings";
    constexpr const char* kVersion = "2.54";
    constexpr const char* kDescription =
        "Search for ANSI and Unicode strings in binary images.";
    constexpr const char* kCopyright = "Independent source-accurate recreation of Strings by Sysinternals, through reverse engineering and rewritten in modern C++20.";
    constexpr const char* kCompany = "Author: 0xntpower";

    struct Config
    {
        bool asciiSearch = false;    // -a selected
        bool unicodeSearch = false;  // -u selected
        bool printOffset = false;    // -o
        bool recurse = false;        // -s
        bool showName = false;       // prefix each line with the file name
        int minLength = 3;           // -n, default 3
        long long startOffset = 0;   // -f, seek before scanning
        long long maxBytes = -1;     // -b, -1 means scan to end
        int filesScanned = 0;        // files opened so far
        int progressState = 0;       // cycles 0..kProgressDots
        bool foundAny = false;       // a string was printed
    };

    // RAII wrapper around a Win32 handle from CreateFile or FindFirstFile.
    class ScopedHandle
    {
    public:
        explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
        ~ScopedHandle()
        {
            if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr)
            {
                CloseHandle(handle_);
            }
        }
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;
        [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
        [[nodiscard]] bool Valid() const noexcept
        {
            return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
        }

    private:
        HANDLE handle_ = INVALID_HANDLE_VALUE;
    };

    // True when stdout is a pipe. The original routes the banner and the usage
    // lines to stdout in that case and to stderr otherwise.
    [[nodiscard]] bool StdoutIsPipe() noexcept
    {
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        return GetFileType(out) == FILE_TYPE_PIPE;
    }

    [[nodiscard]] bool IsPrintableAscii(unsigned char c) noexcept
    {
        return static_cast<unsigned char>(c - kAsciiLow) <= kPrintableSpan;
    }

    // The original tests a little-endian 16-bit value with _ismbcprint and adds
    // back '~' (0x7E), which that routine rejects. Using the same CRT routine
    // keeps the printable classification identical.
    [[nodiscard]] bool IsPrintableWide(unsigned int value) noexcept
    {
        return _ismbcprint(value) != 0 || value == 0x7E;
    }

    void ShowProgress(Config& cfg)
    {
        ++cfg.filesScanned;
        if (cfg.filesScanned % kProgressInterval != 0)
        {
            return;
        }
        if (cfg.progressState == kProgressDots)
        {
            std::fputs("\r     \r", stdout);
            cfg.progressState = 0;
        }
        else
        {
            ++cfg.progressState;
            std::fputs(".", stdout);
        }
        std::fflush(stdout);
    }

    void PrintPrefix(const Config& cfg, const char* name, int offset, const char* offsetFormat)
    {
        if (cfg.showName)
        {
            std::fprintf(stdout, "%s: ", name);
        }
        if (cfg.printOffset)
        {
            std::fprintf(stdout, offsetFormat, offset);
        }
    }

    // Open a single file and walk it for ASCII and Unicode runs. The two run
    // trackers advance together so that once one kind reaches the minimum length
    // the other is suppressed, which is how the original avoids double reporting.
    void ScanFile(DWORD attributes, const char* name, Config& cfg)
    {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            return;
        }

        ScopedHandle file(CreateFileA(name, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!file.Valid())
        {
            std::fprintf(stdout, "\rFailed to open %s: ", name);
            const DWORD error = GetLastError();
            char* message = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&message), 0, nullptr);
            std::fprintf(stdout, "%s\n", message != nullptr ? message : "");
            LocalFree(message);
            return;
        }

        ShowProgress(cfg);

        if (cfg.startOffset != 0)
        {
            LARGE_INTEGER move{};
            move.QuadPart = cfg.startOffset;
            SetFilePointer(file.Get(), move.LowPart, &move.HighPart, FILE_BEGIN);
        }

        DWORD firstRead = kChunkSize;
        if (cfg.maxBytes != -1 && cfg.maxBytes < static_cast<long long>(kChunkSize))
        {
            firstRead = static_cast<DWORD>(cfg.maxBytes);
        }

        char buffer[kBufferSize] = {};
        DWORD bytesRead = 0;
        int base = 0;          // file position of buffer[0] relative to the scan start
        int asciiStart = -1;   // buffer index where the live ASCII run began
        int uniStart = -1;     // buffer index where the live Unicode run began

        if (!ReadFile(file.Get(), buffer, firstRead, &bytesRead, nullptr))
        {
            return;
        }

        DWORD nextRead = firstRead;
        do
        {
            if (bytesRead == 0 || static_cast<int>(bytesRead) < cfg.minLength)
            {
                break;
            }

            int pos = 0;
            for (int i = 0; pos < static_cast<int>(bytesRead); ++i)
            {
                if (cfg.unicodeSearch && (asciiStart == -1 || pos - asciiStart < cfg.minLength))
                {
                    const unsigned int wide = static_cast<unsigned char>(buffer[i])
                        | (static_cast<unsigned int>(static_cast<unsigned char>(buffer[i + 1])) << 8);
                    if (IsPrintableWide(wide))
                    {
                        if (uniStart == -1)
                        {
                            uniStart = pos;
                        }
                        ++pos;
                        ++i;
                    }
                    else if (uniStart != -1)
                    {
                        if (pos - uniStart >= 2 * cfg.minLength)
                        {
                            PrintPrefix(cfg, name, uniStart + base, "%04u:");
                            for (int j = uniStart; j < i; j += 2)
                            {
                                std::fputc(static_cast<unsigned char>(buffer[j]), stdout);
                            }
                            std::fputc('\n', stdout);
                            cfg.foundAny = true;
                        }
                        uniStart = -1;
                    }
                }

                if (cfg.asciiSearch && (uniStart == -1 || pos - uniStart < cfg.minLength))
                {
                    if (!IsPrintableAscii(static_cast<unsigned char>(buffer[i])))
                    {
                        if (asciiStart != -1)
                        {
                            if (pos - asciiStart >= cfg.minLength)
                            {
                                PrintPrefix(cfg, name, asciiStart + base, "%04u:");
                                for (int k = asciiStart; k < i; ++k)
                                {
                                    std::fputc(static_cast<unsigned char>(buffer[k]), stdout);
                                }
                                std::fputc('\n', stdout);
                                cfg.foundAny = true;
                            }
                            asciiStart = -1;
                        }
                    }
                    else if (asciiStart == -1)
                    {
                        asciiStart = pos;
                    }
                }

                ++pos;
            }

            // Flush a run that reaches the end of this window. A run that began at
            // index 0 is a continuation across a window boundary, so it keeps its
            // open state and prints without a trailing newline.
            if (cfg.unicodeSearch
                && ((bytesRead < kChunkSize && uniStart != -1) || uniStart == 0)
                && pos - uniStart >= 2 * cfg.minLength)
            {
                PrintPrefix(cfg, name, uniStart + base, "%04d:");
                for (int j = uniStart; j < pos; j += 2)
                {
                    std::fputc(static_cast<unsigned char>(buffer[j]), stdout);
                }
                if (uniStart != 0)
                {
                    std::fputc('\n', stdout);
                    uniStart = -1;
                }
                cfg.foundAny = true;
            }

            if (cfg.asciiSearch
                && ((bytesRead < kChunkSize && asciiStart != -1) || asciiStart == 0)
                && pos - asciiStart >= cfg.minLength)
            {
                PrintPrefix(cfg, name, asciiStart + base, "%04d:");
                for (int k = asciiStart; k < pos; ++k)
                {
                    std::fputc(static_cast<unsigned char>(buffer[k]), stdout);
                }
                if (asciiStart != 0)
                {
                    std::fputc('\n', stdout);
                    asciiStart = -1;
                }
                cfg.foundAny = true;
            }

            base += static_cast<int>(bytesRead);

            // On a full window with a run still open, rewind the file to the run
            // start and re-read from there so the run is seen whole. The base is
            // adjusted to keep printed offsets anchored to the scan start.
            if (bytesRead == kChunkSize)
            {
                if (asciiStart <= 0)
                {
                    if (uniStart > 0)
                    {
                        SetFilePointer(file.Get(),
                            static_cast<LONG>(uniStart - static_cast<int>(kChunkSize)),
                            nullptr, FILE_CURRENT);
                        base += uniStart - static_cast<int>(bytesRead);
                        uniStart = 0;
                    }
                }
                else
                {
                    SetFilePointer(file.Get(),
                        static_cast<LONG>(asciiStart - static_cast<int>(kChunkSize)),
                        nullptr, FILE_CURRENT);
                    base += asciiStart - static_cast<int>(bytesRead);
                    asciiStart = 0;
                }
            }

            nextRead = kChunkSize;
            if (cfg.maxBytes != -1
                && static_cast<unsigned long long>(cfg.maxBytes - base) <= kChunkSize)
            {
                nextRead = static_cast<DWORD>(cfg.maxBytes - base);
            }
        } while (ReadFile(file.Get(), buffer, nextRead, &bytesRead, nullptr));
    }

    [[nodiscard]] std::string DirectoryOf(const std::string& pattern)
    {
        const std::string::size_type slash = pattern.find_last_of('\\');
        if (slash == std::string::npos)
        {
            return std::string();
        }
        return pattern.substr(0, slash + 1);
    }

    // Expand one path argument. A path with a wildcard, or any path under -s, is
    // enumerated with FindFirstFile and each match is scanned. With -s the walk
    // then descends into every subdirectory. topLevel marks the first call so the
    // nested calls use the plain "dir\*.*" pattern the original builds.
    void Walk(const std::string& path, Config& cfg, bool topLevel)
    {
        std::string filePattern;
        std::string subdirPattern;
        const bool hasWildcard = path.find('*') != std::string::npos;

        if (topLevel)
        {
            if (hasWildcard)
            {
                cfg.showName = true;
                filePattern = path;
                subdirPattern = DirectoryOf(path) + "*.*";
            }
            else if (cfg.recurse)
            {
                cfg.showName = true;
                filePattern = path + "\\*.*";
                subdirPattern = filePattern;
            }
            else
            {
                filePattern = path;
                subdirPattern = path + "\\*.*";
            }
        }
        else
        {
            filePattern = path + "\\*.*";
            subdirPattern = filePattern;
        }

        const std::string fileDir = DirectoryOf(filePattern);

        WIN32_FIND_DATAA find{};
        {
            ScopedHandle handle(FindFirstFileA(filePattern.c_str(), &find));
            if (handle.Valid())
            {
                do
                {
                    if (std::strcmp(find.cFileName, ".") != 0
                        && std::strcmp(find.cFileName, "..") != 0)
                    {
                        const std::string full = fileDir + find.cFileName;
                        ScanFile(find.dwFileAttributes, full.c_str(), cfg);
                    }
                } while (FindNextFileA(handle.Get(), &find));
            }
        }

        if (cfg.recurse)
        {
            const std::string subDir = DirectoryOf(subdirPattern);
            ScopedHandle handle(FindFirstFileA(subdirPattern.c_str(), &find));
            if (handle.Valid())
            {
                do
                {
                    if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                        && std::strcmp(find.cFileName, ".") != 0
                        && std::strcmp(find.cFileName, "..") != 0)
                    {
                        Walk(subDir + find.cFileName, cfg, false);
                    }
                } while (FindNextFileA(handle.Get(), &find));
            }
        }
    }

    [[nodiscard]] FILE* MessageStream()
    {
        return StdoutIsPipe() ? stdout : stderr;
    }

    int PrintUsage(const char* program)
    {
        FILE* out = MessageStream();
        std::fprintf(out,
            "usage: %s [-a] [-f offset] [-b bytes] [-n length] [-o] [-s] [-u] <file or directory>\n",
            program);
        std::fprintf(out, "-a     Ascii-only search (Unicode and Ascii is default)\n");
        std::fprintf(out, "-b     Bytes of file to scan\n");
        std::fprintf(out, "-f     File offset at which to start scanning.\n");
        std::fprintf(out, "-o     Print offset in file string was located\n");
        std::fprintf(out, "-n     Minimum string length (default is 3)\n");
        std::fprintf(out, "-s     Recurse subdirectories\n");
        std::fprintf(out, "-u     Unicode-only search (Unicode and Ascii is default)\n");
        std::fprintf(out, "-nobanner\n");
        std::fprintf(out, "       Do not display the startup banner and copyright message.\n");
        std::fprintf(out, "\n");
        return -1;
    }

    void PrintBanner()
    {
        FILE* out = StdoutIsPipe() ? stdout : stderr;
        std::fprintf(out, "\n%s v%s - %s\n%s\n%s\n\n",
            kName, kVersion, kDescription, kCopyright, kCompany);
        std::fflush(out);
    }

    [[nodiscard]] bool ArgEquals(const char* arg, const char* dash, const char* slash) noexcept
    {
        return _stricmp(arg, dash) == 0 || _stricmp(arg, slash) == 0;
    }

    // Remove the first argument that matches either spelling of a switch, closing
    // the gap in argv and shrinking argc, the way the original strips -nobanner
    // and -accepteula before the main parse.
    [[nodiscard]] bool StripSwitch(int& argc, char** argv, const char* dash, const char* slash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (ArgEquals(argv[i], dash, slash))
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

    // Confirm EULA acceptance. -accepteula or a prior run both let the tool run
    // unattended and write HKCU\Software\Sysinternals\Strings\EulaAccepted. A
    // first run with neither prints the accept notice and stops.
    [[nodiscard]] bool EnsureEula(bool acceptedOnCommandLine)
    {
        bool accepted = acceptedOnCommandLine;

        if (!accepted)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Sysinternals\\Strings",
                0, KEY_READ, &key) == ERROR_SUCCESS)
            {
                DWORD value = 0;
                DWORD size = sizeof(value);
                DWORD type = 0;
                if (RegQueryValueExA(key, "EulaAccepted", nullptr, &type,
                    reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS
                    && type == REG_DWORD && value != 0)
                {
                    accepted = true;
                }
                RegCloseKey(key);
            }
        }

        if (!accepted)
        {
            std::fprintf(stderr, "Strings License Agreement\n");
            std::fprintf(stderr,
                "This is the first run of this program. You must accept EULA to continue.\n");
            std::fprintf(stderr, "Use -accepteula to accept EULA.\n\n");
            return false;
        }

        HKEY key = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Sysinternals\\Strings",
            0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExA(key, "EulaAccepted", 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    const bool noBanner = StripSwitch(argc, argv, "-nobanner", "/nobanner");
    const bool acceptEula = StripSwitch(argc, argv, "-accepteula", "/accepteula");

    if (!noBanner)
    {
        PrintBanner();
    }

    if (!EnsureEula(acceptEula))
    {
        return 1;
    }

    if (argc < 2)
    {
        return PrintUsage(argv[0]);
    }

    Config cfg;
    bool hasPath = false;

    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (ArgEquals(arg, "-s", "/s"))
        {
            cfg.recurse = true;
        }
        else if (ArgEquals(arg, "-a", "/a"))
        {
            cfg.asciiSearch = true;
        }
        else if (ArgEquals(arg, "-o", "/o"))
        {
            cfg.printOffset = true;
        }
        else if (ArgEquals(arg, "-u", "/u"))
        {
            cfg.unicodeSearch = true;
        }
        else if (ArgEquals(arg, "-b", "/b"))
        {
            if (i + 1 >= argc)
            {
                return PrintUsage(argv[0]);
            }
            cfg.maxBytes = std::atoi(argv[++i]);
            if (cfg.maxBytes == 0)
            {
                return PrintUsage(argv[0]);
            }
        }
        else if (ArgEquals(arg, "-f", "/f"))
        {
            if (i + 1 >= argc)
            {
                return PrintUsage(argv[0]);
            }
            cfg.startOffset = std::atoi(argv[++i]);
            if (cfg.startOffset == 0)
            {
                return PrintUsage(argv[0]);
            }
        }
        else if (ArgEquals(arg, "-n", "/n"))
        {
            if (i + 1 >= argc)
            {
                return PrintUsage(argv[0]);
            }
            cfg.minLength = std::atoi(argv[++i]);
            if (cfg.minLength == 0)
            {
                return PrintUsage(argv[0]);
            }
        }
        else if (ArgEquals(arg, "-?", "/?"))
        {
            return PrintUsage(argv[0]);
        }
        else
        {
            if (hasPath)
            {
                return PrintUsage(argv[0]);
            }
            hasPath = true;
        }
    }

    if (!cfg.asciiSearch && !cfg.unicodeSearch)
    {
        cfg.asciiSearch = true;
        cfg.unicodeSearch = true;
    }

    char resolved[MAX_PATH] = {};
    if (hasPath)
    {
        LPSTR filePart = nullptr;
        GetFullPathNameA(argv[argc - 1], MAX_PATH, resolved, &filePart);
    }
    else
    {
        GetCurrentDirectoryA(MAX_PATH, resolved);
    }

    Walk(resolved, cfg, true);

    if (cfg.filesScanned == 0)
    {
        std::fprintf(stderr, "No matching files were found.\n\n");
    }
    else if (!cfg.foundAny)
    {
        std::fprintf(stderr, "No strings found.\n\n");
    }
    return 0;
}
