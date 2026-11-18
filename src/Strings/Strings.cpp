// Strings - console recreation of the Sysinternals Strings utility (v2.54).
// Scans files, wildcards, or directories for runs of printable ASCII and
// little-endian Unicode at least a minimum length long, one per line. See
// analysis/Strings.md for the sub_ address map and validation evidence.

#include "CommandLine.h"
#include "Scan.h"
#include "Usage.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr const char* kName = "Strings";
    constexpr const char* kVersion = "2.54";
    constexpr const char* kDescription = "Search for ANSI and Unicode strings in binary images.";
    constexpr const char* kCopyright =
        "Independent source-accurate recreation of Strings by Sysinternals, through reverse engineering and rewritten "
        "in modern C++20.";
    constexpr const char* kCompany = "Author: 0xntpower";

    void PrintBanner()
    {
        FILE* out = strings::StdoutIsPipe() ? stdout : stderr;
        std::fprintf(out, "\n%s v%s - %s\n%s\n%s\n\n", kName, kVersion, kDescription, kCopyright, kCompany);
        std::fflush(out);
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace strings;

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
