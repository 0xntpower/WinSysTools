// ProcDump shared primitives: the RAII Win32 handle, the Config and Target
// records, the dump-type enum, and the narrow, switch and timestamp helpers.
// See analysis/ProcDump.md for the sub_ map.
#pragma once

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace procdump
{
    constexpr int kDefaultSeconds = 10;

    enum class DumpType
    {
        Mini = 0,
        Full = 1,
        Triage = 2,
        MiniPlus = 3,
        Custom = 4,
        Callback = 5
    };

    // RAII wrapper around a Win32 handle.
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
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
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
                Reset(other.handle_);
                other.handle_ = nullptr;
            }
            return *this;
        }
        void Reset(HANDLE handle) noexcept
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
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
        HANDLE handle_ = nullptr;
    };

    struct Config
    {
        DumpType type = DumpType::Mini;
        bool typeChosen = false;
        DWORD customMask = 0;
        std::wstring callbackDll;

        int count = 1;                 // -n
        int seconds = kDefaultSeconds; // -s

        bool cpuAbove = false; // -c
        bool cpuBelow = false; // -cl
        int cpuThreshold = 0;
        bool singleCore = false; // -u

        bool memAbove = false; // -m
        bool memBelow = false; // -ml
        int commitThresholdMb = 0;

        bool onException = false; // -e
        bool firstChance = false; // -e 1
        bool breakpoints = false; // -b

        bool hung = false;      // -h
        bool terminate = false; // -t

        bool overwrite = false; // -o
        bool wait = false;      // -w

        std::wstring target;
        std::wstring dumpArg;
        bool hasDumpArg = false;
    };

    struct Target
    {
        DWORD pid = 0;
        std::wstring name;      // base name, on-disk case preserved
        std::wstring imagePath; // full image path
    };

    // Narrow conversion for byte-faithful console output. The original is a
    // Unicode app writing narrow bytes to a redirected stdout, which for the
    // ASCII names and paths here is identical.
    [[nodiscard]] inline std::string Narrow(const std::wstring& wide)
    {
        if (wide.empty())
        {
            return {};
        }
        const int needed =
            WideCharToMultiByte(CP_ACP, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), needed, nullptr,
                            nullptr);
        return out;
    }

    [[nodiscard]] inline bool ArgEquals(const wchar_t* arg, const wchar_t* dash) noexcept
    {
        if (_wcsicmp(arg, dash) == 0)
        {
            return true;
        }
        // The original accepts both the '-' and '/' spellings of every switch.
        if (arg[0] == L'/' && dash[0] == L'-')
        {
            return _wcsicmp(arg + 1, dash + 1) == 0;
        }
        return false;
    }

    // Remove the first argv entry matching the switch, shifting the rest down.
    [[nodiscard]] inline bool StripSwitch(int& argc, wchar_t** argv, const wchar_t* dash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (ArgEquals(argv[i], dash))
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

    [[nodiscard]] inline std::string TimeStamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buf[16] = {};
        std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d]", st.wHour, st.wMinute, st.wSecond);
        return buf;
    }
} // namespace procdump
