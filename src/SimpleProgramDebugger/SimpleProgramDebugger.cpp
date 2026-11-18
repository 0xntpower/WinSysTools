// SimpleProgramDebugger.cpp -- entry point, command-line handling and GUI
// bootstrap for the SimpleProgramDebugger recreation (NirSoft lightweight
// debugger, x64). The tool launches or attaches to a target and logs the debug
// events from a WaitForDebugEvent loop. Command-line switches match the original.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include "resource.h"
#include "Config.h"
#include "Columns.h"
#include "DebugEvent.h"
#include "Debugger.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

    void enableDebugPrivilege()
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            return;
        LUID luid{};
        if (::LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid))
        {
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            ::AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        }
        ::CloseHandle(token);
    }

    // Resolve a process name (e.g. explorer.exe) to the first matching pid.
    DWORD pidByName(const std::wstring& name)
    {
        DWORD pid = 0;
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{ sizeof(pe) };
        for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe))
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) { pid = pe.th32ProcessID; break; }
        ::CloseHandle(snap);
        return pid;
    }

    // Returns true if a headless command (export, delete key) was handled.
    bool runCommandLine(int& exitCode, LaunchSpec& pending, bool& hasPending)
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (!argv) return false;

        ExportFormat fmt{};
        std::wstring outFile;
        bool haveExport = false;
        bool deleteKey = false;
        bool langFile = false;
        bool runAsAdmin = false;
        bool startDebug = false;
        std::wstring debugTarget;    // /DebugProcess argument
        std::wstring npPath, npParams, npFolder;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : std::wstring(); };

            if (ieq(a, L"/savelangfile"))              langFile = true;
            else if (ieq(a, L"/RunAsAdmin"))           runAsAdmin = true;
            else if (ieq(a, L"/deleteregkey"))         deleteKey = true;
            else if (ieq(a, L"/DebugProcess"))         debugTarget = next();
            else if (ieq(a, L"/StartDebugProcess"))    startDebug = true;
            else if (ieq(a, L"/NewProcess.ProcessPath")) npPath = next();
            else if (ieq(a, L"/NewProcess.Params"))      npParams = next();
            else if (ieq(a, L"/NewProcess.StartFolder")) npFolder = next();
            else
            {
                ExportFormat f{};
                if (FormatFromSwitch(a, f)) { fmt = f; outFile = next(); haveExport = true; }
            }
        }
        ::LocalFree(argv);

        if (deleteKey)
        {
            Config().deleteConfig();
            exitCode = 0;
            return true;
        }
        if (langFile)
        {
            // Language-file dump is not reconstructed.
            exitCode = 0;
            return true;
        }
        if (runAsAdmin)
        {
            wchar_t self[MAX_PATH];
            ::GetModuleFileNameW(nullptr, self, MAX_PATH);
            ::ShellExecuteW(nullptr, L"runas", self, nullptr, nullptr, SW_SHOWNORMAL);
            exitCode = 0;
            return true;
        }
        if (haveExport)
        {
            // The export writes the current event list, which is empty in this
            // headless invocation, so the report carries only its header.
            Config cfg;
            cfg.load();
            ExportOptions opt;
            opt.addHeaderLine = cfg.addExportHeaderLine;
            std::vector<DbgRow> empty;
            bool ok = WriteReport(outFile, empty, cfg.visibleColumns, fmt, opt);
            exitCode = ok ? 0 : 1;
            return true;
        }

        // Interactive debug requests feed the window a pending launch.
        if (!debugTarget.empty())
        {
            pending.attach = true;
            bool numeric = !debugTarget.empty() && debugTarget.find_first_not_of(L"0123456789") == std::wstring::npos;
            pending.pid = numeric ? static_cast<DWORD>(_wtoi(debugTarget.c_str())) : pidByName(debugTarget);
            hasPending = pending.pid != 0;
        }
        else if (startDebug)
        {
            Config cfg;
            cfg.load();
            pending.attach = false;
            pending.path = !npPath.empty() ? npPath : cfg.newProcessPath;
            pending.params = !npParams.empty() ? npParams : cfg.newProcessParams;
            pending.startFolder = !npFolder.empty() ? npFolder : cfg.newProcessStartFolder;
            hasPending = !pending.path.empty();
        }
        return false;
    }

    int runGui(int nCmdShow, const LaunchSpec& pending, bool hasPending)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_hasPending = hasPending;
        frame.m_pendingLaunch = pending;
        // The original CreateWindowExW passes CW_USEDEFAULT for X, Y, width and
        // height, so let WTL use its default rect.
        if (frame.CreateEx() == nullptr)
            return 0;
        int show = frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow;
        if (frame.m_cfg.startAsHidden) show = SW_HIDE;
        frame.ShowWindow(show);
        frame.UpdateWindow();

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Error", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    enableDebugPrivilege();

    int exitCode = 0;
    LaunchSpec pending;
    bool hasPending = false;
    if (runCommandLine(exitCode, pending, hasPending))
    {
        _Module.Term();
        return exitCode;
    }

    int ret = runGui(nCmdShow, pending, hasPending);
    _Module.Term();
    return ret;
}
