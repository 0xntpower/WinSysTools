// LiveKd shared primitives: the base includes, the error exit code, the RAII
// Win32 handle (shared shape with ProcDump), the Config record, and the small
// argument and narrow-conversion helpers. See analysis/LiveKd.md for the sub_ map.
#pragma once

#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace livekd
{
    constexpr int kExitError = -1;

    // RAII wrapper around a Win32 handle (shared shape with ProcDump).
    class ScopedHandle
    {
      public:
        ScopedHandle() noexcept = default;
        explicit ScopedHandle(HANDLE handle) noexcept
            : handle_(handle)
        {
        }
        ~ScopedHandle()
        {
            Close();
        }
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;
        ScopedHandle(ScopedHandle&& other) noexcept
            : handle_(other.handle_)
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
        [[nodiscard]] HANDLE Get() const noexcept
        {
            return handle_;
        }
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
        bool useWindbg = false;        // -w
        bool batch = false;            // -b
        bool dumpToFile = false;       // -o <file>
        std::string dumpFile;          // -o argument (fully qualified)
        bool nativeLive = false;       // -ml
        bool hypervisorPages = false;  // -hvd
        bool dumpStorageStack = false; // -md
        bool verboseSymbols = false;   // -vsym
        std::string debuggerPath;      // -k <path>
        std::string debuggerBase;      // "kd", "windbg", or basename from -k
        std::string passthrough;       // trailing debugger options
    };

    [[nodiscard]] inline bool ArgEquals(const wchar_t* arg, const wchar_t* dash) noexcept
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

    // Narrow conversion so a console pipe sees the same bytes.
    [[nodiscard]] inline std::string Narrow(const wchar_t* wide)
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
} // namespace livekd
