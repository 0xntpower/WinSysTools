// Capture.h -- the Win32 user-mode debug capture, the path that needs no driver.
//
// It reproduces the DBWIN monitor protocol recovered from Dbgview64.exe
// (sub_140005710 and sub_14000A960): a 4 KB shared section named DBWIN_BUFFER
// holds the writer pid in the first four bytes followed by the ANSI message,
// and two auto-reset events coordinate the hand-off. The monitor signals
// DBWIN_BUFFER_READY to mark the buffer free, waits on DBWIN_DATA_READY which a
// process raises from inside OutputDebugStringA after copying its line, reads
// the record, then signals buffer-ready again. A permissive security
// descriptor, matching the original, lets writers of any integrity level reach
// the objects.
#pragma once
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include "DbgRow.h"

#pragma comment(lib, "advapi32.lib")

class DbwinCapture
{
public:
    DbwinCapture() = default;
    DbwinCapture(const DbwinCapture&) = delete;
    DbwinCapture& operator=(const DbwinCapture&) = delete;
    ~DbwinCapture() { stop(); }

    bool running() const noexcept { return thread_ != nullptr; }

    // Create the shared objects and start the reader thread. Each captured line
    // is posted to hwnd as msg with lParam a heap DbgRow the window takes over.
    bool start(HWND hwnd, UINT msg)
    {
        if (thread_)
            return true;

        hwnd_ = hwnd;
        msg_  = msg;
        ::QueryPerformanceFrequency(&freq_);

        SECURITY_ATTRIBUTES sa{};
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GRGWGX;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGWGX;;;AN)"
                L"(A;;GRGWGX;;;RC)(A;;GRGWGX;;;S-1-15-2-1)S:(ML;;NW;;;LW)",
                SDDL_REVISION_1, &sd, nullptr))
        {
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = sd;
            sa.bInheritHandle = FALSE;
        }
        SECURITY_ATTRIBUTES* psa = sd ? &sa : nullptr;

        bool ok = createObjects(psa);
        if (sd)
            ::LocalFree(sd);
        if (!ok)
        {
            cleanup();
            return false;
        }

        stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_)
        {
            cleanup();
            return false;
        }

        thread_ = ::CreateThread(nullptr, 0, &DbwinCapture::threadEntry, this, 0, nullptr);
        if (!thread_)
        {
            cleanup();
            return false;
        }
        return true;
    }

    void stop()
    {
        if (thread_)
        {
            if (stopEvent_)
                ::SetEvent(stopEvent_);
            ::WaitForSingleObject(thread_, 2000);
            ::CloseHandle(thread_);
            thread_ = nullptr;
        }
        cleanup();
    }

private:
    bool createObjects(SECURITY_ATTRIBUTES* psa)
    {
        // A named mutex marks the single live monitor, exactly as the original.
        hMutex_ = ::CreateMutexW(psa, FALSE, L"DBWinMutex");

        hMap_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, psa, PAGE_READWRITE,
                                     0, kBufferSize, L"DBWIN_BUFFER");
        if (!hMap_)
            return false;

        buffer_ = ::MapViewOfFile(hMap_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kBufferSize);
        if (!buffer_)
            return false;

        hDataReady_   = ::CreateEventW(psa, FALSE, FALSE, L"DBWIN_DATA_READY");
        hBufferReady_ = ::CreateEventW(psa, FALSE, FALSE, L"DBWIN_BUFFER_READY");
        return hDataReady_ && hBufferReady_;
    }

    void cleanup()
    {
        if (stopEvent_)    { ::CloseHandle(stopEvent_);    stopEvent_ = nullptr; }
        if (buffer_)       { ::UnmapViewOfFile(buffer_);   buffer_ = nullptr; }
        if (hMap_)         { ::CloseHandle(hMap_);         hMap_ = nullptr; }
        if (hDataReady_)   { ::CloseHandle(hDataReady_);   hDataReady_ = nullptr; }
        if (hBufferReady_) { ::CloseHandle(hBufferReady_); hBufferReady_ = nullptr; }
        if (hMutex_)       { ::CloseHandle(hMutex_);       hMutex_ = nullptr; }
    }

    static DWORD WINAPI threadEntry(LPVOID self)
    {
        static_cast<DbwinCapture*>(self)->readerLoop();
        return 0;
    }

    void readerLoop()
    {
        // Mark the buffer free so the first writer may proceed.
        ::SetEvent(hBufferReady_);

        HANDLE waits[2] = { hDataReady_, stopEvent_ };
        for (;;)
        {
            DWORD r = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (r != WAIT_OBJECT_0)
                break;   // stop signalled or a wait failure

            // Snapshot the record before releasing the buffer back to writers.
            const BYTE* p = static_cast<const BYTE*>(buffer_);
            DWORD pid = *reinterpret_cast<const DWORD*>(p);
            const char* ansi = reinterpret_cast<const char*>(p + sizeof(DWORD));
            size_t maxLen = kBufferSize - sizeof(DWORD);
            size_t len = ::strnlen(ansi, maxLen);
            std::string msg(ansi, len);

            FILETIME ft{};
            ::GetSystemTimeAsFileTime(&ft);
            LARGE_INTEGER now{};
            ::QueryPerformanceCounter(&now);

            // Release the buffer right away so capture keeps up with the writer.
            ::SetEvent(hBufferReady_);

            emit(pid, msg, ft, now.QuadPart);
        }
    }

    // Split one record into display lines and post each to the window. A record
    // without an internal newline becomes a single line, matching the original's
    // default (Force Carriage Returns) behaviour.
    void emit(DWORD pid, const std::string& msg, FILETIME ft, long long qpc)
    {
        size_t start = 0;
        const size_t n = msg.size();
        // Drop a single trailing CR or LF so a plain "line\n" yields one row.
        size_t end = n;
        while (end > start && (msg[end - 1] == '\n' || msg[end - 1] == '\r'))
            --end;

        if (end == start)
        {
            post(pid, std::wstring(), ft, qpc);
            return;
        }
        for (size_t i = start; i <= end; ++i)
        {
            if (i == end || msg[i] == '\n')
            {
                size_t segEnd = i;
                if (segEnd > start && msg[segEnd - 1] == '\r')
                    --segEnd;
                post(pid, widen(msg.substr(start, segEnd - start)), ft, qpc);
                start = i + 1;
            }
        }
    }

    void post(DWORD pid, std::wstring text, FILETIME ft, long long qpc)
    {
        DbgRow* row = new DbgRow();
        row->pid = pid;
        row->ft = ft;
        row->qpc = qpc;
        row->message = std::move(text);
        if (!::PostMessageW(hwnd_, msg_, 0, reinterpret_cast<LPARAM>(row)))
            delete row;   // window gone, drop the row rather than leak
    }

    static std::wstring widen(const std::string& a)
    {
        if (a.empty()) return std::wstring();
        int n = ::MultiByteToWideChar(CP_ACP, 0, a.c_str(), static_cast<int>(a.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, a.c_str(), static_cast<int>(a.size()), w.data(), n);
        return w;
    }

    static constexpr DWORD kBufferSize = 4096;

    HWND          hwnd_        = nullptr;
    UINT          msg_         = 0;
    HANDLE        hMap_        = nullptr;
    void*         buffer_      = nullptr;
    HANDLE        hDataReady_  = nullptr;
    HANDLE        hBufferReady_ = nullptr;
    HANDLE        hMutex_      = nullptr;
    HANDLE        stopEvent_   = nullptr;
    HANDLE        thread_      = nullptr;
    LARGE_INTEGER freq_        { };
};
