// Debugger.cpp -- the WaitForDebugEvent loop and per-event column formatting.
//
// Field-to-column mapping and formatting were recovered from the original binary
// and confirmed by running it against a controlled target:
//  Event Time      elapsed HH:MM:SS.mmm since the session start baseline
//  Event Type      the name of the debug event (Exception, Load DLL and so on)
//  Thread ID       dwThreadId in decimal
//  Memory Address  exception address, thread start, or DLL base as hi`lo hex,
//                  empty for the process and debug-string events
//  Filename        the module path for Load DLL and Unload Dll, empty otherwise
//  More Information the debug string, the exception code, or the exit code
#include "Debugger.h"
#include "DebugEvent.h"
#include "Columns.h"
#include "resource.h"
#include <psapi.h>
#include <map>
#include <vector>

namespace
{
    long long filetimeToLL(const FILETIME& ft)
    {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return static_cast<long long>(u.QuadPart);
    }

    long long nowLL()
    {
        FILETIME ft;
        ::GetSystemTimeAsFileTime(&ft);
        return filetimeToLL(ft);
    }

    // The original prints the elapsed time as hours:minutes:seconds.milliseconds,
    // computed from the 100ns delta via the same floating-point split.
    std::wstring fmtElapsed(long long delta100ns)
    {
        double v = static_cast<double>(static_cast<int>(delta100ns)) / 10000000.0;
        int h = static_cast<int>(v / 3600.0);
        v -= 3600.0 * h;
        int m = static_cast<int>(v / 60.0);
        v -= 60.0 * m;
        int s = static_cast<int>(v);
        int ms = static_cast<int>((v - s) * 1000.0);
        wchar_t buf[64];
        swprintf(buf, 64, L"%.2d:%.2d:%2.2d.%.3d", h, m, s, ms);
        return buf;
    }

    // The 64-bit build always prints addresses as the windbg hi`lo form, matching
    // the original. A zero address renders as an empty cell.
    std::wstring fmtAddr(ULONGLONG addr)
    {
        if (addr == 0) return std::wstring();
        wchar_t buf[32];
        swprintf(buf, 32, L"%8.8x`%8.8x",
                 static_cast<unsigned>(addr >> 32), static_cast<unsigned>(addr & 0xFFFFFFFF));
        return buf;
    }

    std::wstring hex8(DWORD v)
    {
        wchar_t buf[16];
        swprintf(buf, 16, L"0x%8.8x", v);
        return buf;
    }

    std::wstring stripDevicePrefix(std::wstring p)
    {
        if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + p.substr(8);
        if (p.rfind(L"\\\\?\\", 0) == 0) return p.substr(4);
        return p;
    }

    // Resolve a module path. The loader list read by GetModuleFileNameEx carries
    // the case the loader recorded, matching the original, but it is only
    // queryable once the loader has settled, so this runs after ContinueDebugEvent.
    // The file handle from the debug event is the reliable fallback for the very
    // early load notifications where the loader list is not ready yet.
    std::wstring resolveModule(HANDLE hProc, DWORD pid, LPVOID base, HANDLE hFile)
    {
        wchar_t buf[1024];
        if (base)
        {
            if (hProc)
            {
                DWORD n = ::GetModuleFileNameExW(hProc, static_cast<HMODULE>(base), buf, 1024);
                if (n != 0) return std::wstring(buf, n);
            }
            if (HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid))
            {
                DWORD n = ::GetModuleFileNameExW(h, static_cast<HMODULE>(base), buf, 1024);
                ::CloseHandle(h);
                if (n != 0) return std::wstring(buf, n);
            }
        }
        if (hFile && hFile != INVALID_HANDLE_VALUE)
        {
            DWORD n = ::GetFinalPathNameByHandleW(hFile, buf, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (n != 0 && n < 1024) return stripDevicePrefix(std::wstring(buf, n));
        }
        return std::wstring();
    }

    std::wstring readDebugString(HANDLE hProc, const OUTPUT_DEBUG_STRING_INFO& info)
    {
        if (!hProc || !info.lpDebugStringData || info.nDebugStringLength == 0)
            return std::wstring();
        const WORD len = info.nDebugStringLength;
        if (info.fUnicode)
        {
            std::vector<wchar_t> wbuf(len + 1, L'\0');
            SIZE_T got = 0;
            if (!::ReadProcessMemory(hProc, info.lpDebugStringData, wbuf.data(),
                                     static_cast<SIZE_T>(len) * sizeof(wchar_t), &got))
                return std::wstring();
            std::wstring s(wbuf.data(), wcsnlen(wbuf.data(), len));
            while (!s.empty() && s.back() == L'\0') s.pop_back();
            return s;
        }
        std::vector<char> abuf(len + 1, '\0');
        SIZE_T got = 0;
        if (!::ReadProcessMemory(hProc, info.lpDebugStringData, abuf.data(), len, &got))
            return std::wstring();
        size_t alen = strnlen(abuf.data(), len);
        int wn = ::MultiByteToWideChar(CP_ACP, 0, abuf.data(), static_cast<int>(alen), nullptr, 0);
        std::wstring s(wn, L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, abuf.data(), static_cast<int>(alen), s.data(), wn);
        while (!s.empty() && s.back() == L'\0') s.pop_back();
        return s;
    }
}

void Debugger::start(const LaunchSpec& spec, HWND notify)
{
    stop();
    m_notify = notify;
    m_stopReq = false;
    m_thread = std::thread(&Debugger::run, this, spec);
}

void Debugger::stop()
{
    m_stopReq = true;
    if (m_thread.joinable())
        m_thread.join();
    m_active = false;
}

void Debugger::run(LaunchSpec spec)
{
    DWORD mainPid = 0;

    if (spec.attach)
    {
        if (!::DebugActiveProcess(spec.pid))
        {
            ::PostMessageW(m_notify, WM_SPD_STATUS, static_cast<WPARAM>(SpdStatus::SPD_ERROR), static_cast<LPARAM>(::GetLastError()));
            return;
        }
        mainPid = spec.pid;
    }
    else
    {
        std::wstring exe = spec.path;
        if (exe.find(L'%') != std::wstring::npos)
        {
            wchar_t expanded[1024];
            if (::ExpandEnvironmentStringsW(exe.c_str(), expanded, 1024))
                exe = expanded;
        }
        std::wstring cmd = spec.params.empty() ? exe : (L"\"" + exe + L"\" " + spec.params);
        std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
        cmdbuf.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        LPCWSTR cwd = spec.startFolder.empty() ? nullptr : spec.startFolder.c_str();
        // DEBUG_PROCESS so child processes are debugged too, as the original does.
        if (!::CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE,
                              DEBUG_PROCESS, nullptr, cwd, &si, &pi))
        {
            ::PostMessageW(m_notify, WM_SPD_STATUS, static_cast<WPARAM>(SpdStatus::SPD_ERROR), static_cast<LPARAM>(::GetLastError()));
            return;
        }
        mainPid = pi.dwProcessId;
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }

    m_pid = mainPid;
    m_active = true;
    ::PostMessageW(m_notify, WM_SPD_STATUS, static_cast<WPARAM>(SpdStatus::SPD_STARTED), static_cast<LPARAM>(mainPid));

    const long long base = nowLL();
    std::map<DWORD, HANDLE> procHandles;         // process id -> hProcess from CREATE_PROCESS
    std::map<ULONGLONG, std::wstring> moduleNames; // dll base -> path, for Unload lookup

    DEBUG_EVENT de{};
    for (;;)
    {
        if (m_stopReq.load())
            break;
        if (!::WaitForDebugEvent(&de, 10))
            continue;

        if (de.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
        {
            procHandles[de.dwProcessId] = de.u.CreateProcessInfo.hProcess;
            if (de.u.CreateProcessInfo.hFile)
                ::CloseHandle(de.u.CreateProcessInfo.hFile);
        }

        const bool isBreakpoint =
            de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT;

        HANDLE hProc = procHandles.count(de.dwProcessId) ? procHandles[de.dwProcessId] : nullptr;
        DbgRow* row = nullptr;
        ULONGLONG loadBase = 0;
        HANDLE loadFile = nullptr;

        if (!isBreakpoint && de.dwDebugEventCode >= 1 && de.dwDebugEventCode <= 8)
        {
            row = new DbgRow();
            long long t = nowLL();
            row->timeKey = t;
            row->tid = de.dwThreadId;
            row->cells[0] = fmtElapsed(t - base);
            row->cells[1] = kEventTypeNames[de.dwDebugEventCode];
            row->cells[2] = std::to_wstring(de.dwThreadId);

            ULONGLONG addr = 0;
            switch (de.dwDebugEventCode)
            {
            case EXCEPTION_DEBUG_EVENT:
                addr = reinterpret_cast<ULONGLONG>(de.u.Exception.ExceptionRecord.ExceptionAddress);
                row->cells[5] = L"Exception Code: " + hex8(de.u.Exception.ExceptionRecord.ExceptionCode);
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                addr = reinterpret_cast<ULONGLONG>(de.u.CreateThread.lpStartAddress);
                break;
            case LOAD_DLL_DEBUG_EVENT:
                // Resolve the module path after the loader settles, below.
                addr = reinterpret_cast<ULONGLONG>(de.u.LoadDll.lpBaseOfDll);
                loadBase = addr;
                loadFile = de.u.LoadDll.hFile;
                break;
            case UNLOAD_DLL_DEBUG_EVENT:
            {
                addr = reinterpret_cast<ULONGLONG>(de.u.UnloadDll.lpBaseOfDll);
                auto it = moduleNames.find(addr);
                if (it != moduleNames.end())
                    row->cells[4] = it->second;
                break;
            }
            case EXIT_THREAD_DEBUG_EVENT:
                row->cells[5] = L"Exit Code: " + hex8(de.u.ExitThread.dwExitCode);
                break;
            case EXIT_PROCESS_DEBUG_EVENT:
                row->cells[5] = L"Exit Code: " + hex8(de.u.ExitProcess.dwExitCode);
                break;
            case OUTPUT_DEBUG_STRING_EVENT:
                row->cells[5] = readDebugString(hProc, de.u.DebugString);
                break;
            default:
                break;
            }
            row->cells[3] = fmtAddr(addr);
        }
        else if (de.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && de.u.LoadDll.hFile)
        {
            ::CloseHandle(de.u.LoadDll.hFile);
        }

        const DWORD cont = isBreakpoint ? DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED;
        ::ContinueDebugEvent(de.dwProcessId, de.dwThreadId, cont);

        if (row)
        {
            if (de.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT)
            {
                std::wstring path = resolveModule(hProc, de.dwProcessId, reinterpret_cast<LPVOID>(loadBase), loadFile);
                if (!path.empty())
                    moduleNames[loadBase] = path;
                row->cells[4] = path;
                if (loadFile)
                    ::CloseHandle(loadFile);
            }
            ::PostMessageW(m_notify, WM_SPD_EVENT, 0, reinterpret_cast<LPARAM>(row));
        }

        if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
        {
            procHandles.erase(de.dwProcessId);
            if (de.dwProcessId == mainPid)
                break;
        }
    }

    ::DebugSetProcessKillOnExit(FALSE);
    ::DebugActiveProcessStop(mainPid);
    m_active = false;
    ::PostMessageW(m_notify, WM_SPD_STATUS, static_cast<WPARAM>(SpdStatus::SPD_ENDED), 0);
}
