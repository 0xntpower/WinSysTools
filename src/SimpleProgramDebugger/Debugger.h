// Debugger.h -- the lightweight debug engine. It launches or attaches to a
// target, runs a WaitForDebugEvent loop on a worker thread, formats every debug
// event into a DbgRow, and posts it to the window with WM_SPD_EVENT. Status
// changes (start, end, error) are posted with WM_SPD_STATUS. This mirrors the
// original's background debug thread and its PostMessage-per-event design.
#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>

enum class SpdStatus { SPD_ERROR = 1, SPD_STARTED = 2, SPD_ENDED = 3 };

struct LaunchSpec
{
    bool         attach = false;   // true -> attach to pid, false -> launch path
    DWORD        pid = 0;
    std::wstring path;
    std::wstring params;
    std::wstring startFolder;
};

class Debugger
{
public:
    ~Debugger() { stop(); }

    // Begin a session. notify receives WM_SPD_EVENT and WM_SPD_STATUS messages.
    void start(const LaunchSpec& spec, HWND notify);
    // Request detach and wait for the worker to finish.
    void stop();
    bool active() const { return m_active.load(); }
    DWORD pid() const { return m_pid.load(); }

private:
    void run(LaunchSpec spec);

    std::thread       m_thread;
    std::atomic<bool> m_stopReq{ false };
    std::atomic<bool> m_active{ false };
    std::atomic<DWORD> m_pid{ 0 };
    HWND              m_notify = nullptr;
};
