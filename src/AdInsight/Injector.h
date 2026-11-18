// Injector.h -- drops the embedded hook DLL and places it into a process.
//
// This mirrors the original's structure: the GUI carries ADInsightDll.dll as a
// BINRES resource, writes it to the temp directory, schedules it for delete on
// reboot, and loads it. The original then installs a system-wide
// SetWindowsHookEx to auto-load the DLL into every process. That all-process
// path is out of scope (see analysis/AdInsight.md). This core uses targeted
// CreateRemoteThread injection, exercised only against a benign test process.
#pragma once

#include <windows.h>
#include <string>
#include "resource.h"
#include "TraceProtocol.h"

namespace adin
{
    // Resolved exports from the loaded hook DLL, matching the original names.
    struct HookDll
    {
        HMODULE module = nullptr;
        using InitFn = int(__cdecl*)(void*, void*, void*, int);
        using VoidFn = void(__cdecl*)();
        InitFn traceInit = nullptr;
        VoidFn monitorStart = nullptr;
        VoidFn monitorStop = nullptr;
        VoidFn pingDll = nullptr;

        [[nodiscard]] bool Resolve()
        {
            if (module == nullptr)
            {
                return false;
            }
            traceInit = reinterpret_cast<InitFn>(::GetProcAddress(module, kExportTraceInit));
            monitorStart = reinterpret_cast<VoidFn>(::GetProcAddress(module, kExportMonitorStart));
            monitorStop = reinterpret_cast<VoidFn>(::GetProcAddress(module, kExportMonitorStop));
            pingDll = reinterpret_cast<VoidFn>(::GetProcAddress(module, kExportPingDll));
            return traceInit != nullptr;
        }
    };

    // Extract the embedded hook DLL to <temp>\ADInsightDll.dll. Returns the
    // written path, or an empty string on failure.
    inline std::wstring ExtractHookDll()
    {
        wchar_t temp[MAX_PATH] = {};
        if (::GetTempPathW(MAX_PATH, temp) == 0)
        {
            return std::wstring();
        }
        std::wstring path = temp;
        path += kHookDllName;

        HRSRC res = ::FindResourceW(nullptr, MAKEINTRESOURCEW(kHookDllResId), kResTypeBinres);
        if (res == nullptr)
        {
            return std::wstring();
        }
        HGLOBAL loaded = ::LoadResource(nullptr, res);
        const DWORD size = ::SizeofResource(nullptr, res);
        const void* data = ::LockResource(loaded);
        if (data == nullptr || size == 0)
        {
            return std::wstring();
        }

        // If a prior run (for example the GUI that just loaded the DLL) already
        // dropped an identical copy, reuse it. The file is held open by that
        // load, so rewriting it would fail with a sharing violation.
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) && fad.nFileSizeHigh == 0 &&
            fad.nFileSizeLow == size)
        {
            return path;
        }

        ::DeleteFileW(path.c_str());
        HANDLE f =
            ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
        {
            return std::wstring();
        }
        DWORD written = 0;
        const BOOL ok = ::WriteFile(f, data, size, &written, nullptr);
        ::CloseHandle(f);
        if (!ok || written != size)
        {
            return std::wstring();
        }
        // Clean up on reboot, matching the original's MoveFileEx call.
        ::MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        return path;
    }

    // Load the hook DLL into the GUI's own process and resolve its exports,
    // matching the original's LoadLibrary plus GetProcAddress sequence.
    inline bool LoadSelf(const std::wstring& dllPath, HookDll& out)
    {
        out.module = ::LoadLibraryW(dllPath.c_str());
        return out.Resolve();
    }

    // Place the hook DLL into a target process by its PID via
    // CreateRemoteThread(LoadLibraryW). Used only against a benign test process.
    [[nodiscard]] inline bool InjectIntoPid(DWORD pid, const std::wstring& dllPath)
    {
        HANDLE proc = ::OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                        PROCESS_VM_WRITE | PROCESS_VM_READ,
                                    FALSE, pid);
        if (proc == nullptr)
        {
            return false;
        }

        bool ok = false;
        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remote = ::VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (remote != nullptr)
        {
            SIZE_T wrote = 0;
            if (::WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, &wrote) && wrote == bytes)
            {
                auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                    reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
                if (loadLib != nullptr)
                {
                    HANDLE thread = ::CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
                    if (thread != nullptr)
                    {
                        ::WaitForSingleObject(thread, 5000);
                        DWORD code = 0;
                        ::GetExitCodeThread(thread, &code);
                        ok = code != 0; // LoadLibraryW returned a non-null HMODULE
                        ::CloseHandle(thread);
                    }
                }
            }
            ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        }
        ::CloseHandle(proc);
        return ok;
    }
} // namespace adin
