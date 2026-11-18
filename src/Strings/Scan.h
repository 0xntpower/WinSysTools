// Strings scan engine: the per-file byte walker and the directory/wildcard
// walker that feeds it (mirrors sub_1400037E0 and sub_1400033B0).
#pragma once

#include "StringsUtil.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace strings
{
    inline void ShowProgress(Config& cfg)
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

    inline void PrintPrefix(const Config& cfg, const char* name, int offset, const char* offsetFormat)
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
    inline void ScanFile(DWORD attributes, const char* name, Config& cfg)
    {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            return;
        }

        ScopedHandle file(
            CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!file.Valid())
        {
            std::fprintf(stdout, "\rFailed to open %s: ", name);
            const DWORD error = GetLastError();
            char* message = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error,
                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&message), 0, nullptr);
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
        int base = 0;        // file position of buffer[0] relative to the scan start
        int asciiStart = -1; // buffer index where the live ASCII run began
        int uniStart = -1;   // buffer index where the live Unicode run began

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
                    const unsigned int wide =
                        static_cast<unsigned char>(buffer[i]) |
                        (static_cast<unsigned int>(static_cast<unsigned char>(buffer[i + 1])) << 8);
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
            // index 0 is a continuation across a window boundary, so it stays open
            // and prints without a trailing newline.
            if (cfg.unicodeSearch && ((bytesRead < kChunkSize && uniStart != -1) || uniStart == 0) &&
                pos - uniStart >= 2 * cfg.minLength)
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

            if (cfg.asciiSearch && ((bytesRead < kChunkSize && asciiStart != -1) || asciiStart == 0) &&
                pos - asciiStart >= cfg.minLength)
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
            // adjusted so printed offsets stay anchored to the scan start.
            if (bytesRead == kChunkSize)
            {
                if (asciiStart <= 0)
                {
                    if (uniStart > 0)
                    {
                        SetFilePointer(file.Get(), static_cast<LONG>(uniStart - static_cast<int>(kChunkSize)), nullptr,
                                       FILE_CURRENT);
                        base += uniStart - static_cast<int>(bytesRead);
                        uniStart = 0;
                    }
                }
                else
                {
                    SetFilePointer(file.Get(), static_cast<LONG>(asciiStart - static_cast<int>(kChunkSize)), nullptr,
                                   FILE_CURRENT);
                    base += asciiStart - static_cast<int>(bytesRead);
                    asciiStart = 0;
                }
            }

            nextRead = kChunkSize;
            if (cfg.maxBytes != -1 && static_cast<unsigned long long>(cfg.maxBytes - base) <= kChunkSize)
            {
                nextRead = static_cast<DWORD>(cfg.maxBytes - base);
            }
        } while (ReadFile(file.Get(), buffer, nextRead, &bytesRead, nullptr));
    }

    [[nodiscard]] inline std::string DirectoryOf(const std::string& pattern)
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
    inline void Walk(const std::string& path, Config& cfg, bool topLevel)
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
                    if (std::strcmp(find.cFileName, ".") != 0 && std::strcmp(find.cFileName, "..") != 0)
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
                    if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                        std::strcmp(find.cFileName, ".") != 0 && std::strcmp(find.cFileName, "..") != 0)
                    {
                        Walk(subDir + find.cFileName, cfg, false);
                    }
                } while (FindNextFileA(handle.Get(), &find));
            }
        }
    }
} // namespace strings
