// Strings shared leaf helpers: the scan constants, the Config state, the RAII
// Win32 handle, the stdout-pipe probe, and the printable classifiers.
// See analysis/Strings.md for the sub_ address map.
#pragma once

#include <windows.h>

#include <mbstring.h>

namespace strings
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

    struct Config
    {
        bool asciiSearch = false;   // -a selected
        bool unicodeSearch = false; // -u selected
        bool printOffset = false;   // -o
        bool recurse = false;       // -s
        bool showName = false;      // prefix each line with the file name
        int minLength = 3;          // -n, default 3
        long long startOffset = 0;  // -f, seek before scanning
        long long maxBytes = -1;    // -b, -1 means scan to end
        int filesScanned = 0;       // files opened so far
        int progressState = 0;      // cycles 0..kProgressDots
        bool foundAny = false;      // a string was printed
    };

    // RAII wrapper around a Win32 handle from CreateFile or FindFirstFile.
    class ScopedHandle
    {
      public:
        explicit ScopedHandle(HANDLE handle) noexcept
            : handle_(handle)
        {
        }
        ~ScopedHandle()
        {
            if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr)
            {
                CloseHandle(handle_);
            }
        }
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;
        [[nodiscard]] HANDLE Get() const noexcept
        {
            return handle_;
        }
        [[nodiscard]] bool Valid() const noexcept
        {
            return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
        }

      private:
        HANDLE handle_ = INVALID_HANDLE_VALUE;
    };

    // True when stdout is a pipe. The original routes the banner and the usage
    // lines to stdout in that case and to stderr otherwise.
    [[nodiscard]] inline bool StdoutIsPipe() noexcept
    {
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        return GetFileType(out) == FILE_TYPE_PIPE;
    }

    [[nodiscard]] inline bool IsPrintableAscii(unsigned char c) noexcept
    {
        return static_cast<unsigned char>(c - kAsciiLow) <= kPrintableSpan;
    }

    // Tests a little-endian 16-bit value with _ismbcprint, adding back 0x7E ('~')
    // which that CRT routine rejects, so the classification matches the original.
    [[nodiscard]] inline bool IsPrintableWide(unsigned int value) noexcept
    {
        return _ismbcprint(value) != 0 || value == 0x7E;
    }
} // namespace strings
