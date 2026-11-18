// VMMap.cpp -- entry point, EULA gate and GUI bootstrap for the VMMap
// recreation, a source-accurate process virtual-memory analyzer (x64).
//
// The command line carries -accepteula (both -accepteula and /accepteula
// spellings), which records acceptance without prompting, and -p <pid or
// process name> to snapshot a process on startup. Acceptance is stored under
// HKCU\Software\Sysinternals\VMMap, value EulaAccepted (REG_DWORD 1), exactly
// as the original does.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <string>
#include <vector>
#include "resource.h"
#include "MainFrame.h"
#include "ProcessList.h"
#include "Export.h"
#include "Eula.h"

CAppModule _Module;

namespace
{
    std::vector<std::wstring> Args()
    {
        std::vector<std::wstring> v;
        int n = 0;
        if (LPWSTR* a = ::CommandLineToArgvW(::GetCommandLineW(), &n))
        {
            for (int i = 1; i < n; ++i)
            {
                v.emplace_back(a[i]);
            }
            ::LocalFree(a);
        }
        return v;
    }

    bool HasFlag(const std::vector<std::wstring>& a, const wchar_t* flag)
    {
        for (const std::wstring& s : a)
        {
            if (_wcsicmp(s.c_str(), flag) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // Resolve a -p argument (numeric PID or process-name substring) to a PID.
    // An argument after the target that is not itself a switch is taken as an
    // output file for a headless dump, matching the original's command line.
    DWORD ResolveTarget(const std::vector<std::wstring>& a, std::wstring& nameOut, std::wstring& outFile)
    {
        for (size_t i = 0; i + 1 < a.size(); ++i)
        {
            if (_wcsicmp(a[i].c_str(), L"-p") == 0 || _wcsicmp(a[i].c_str(), L"/p") == 0)
            {
                const std::wstring& want = a[i + 1];
                if (i + 2 < a.size() && a[i + 2][0] != L'-' && a[i + 2][0] != L'/')
                {
                    outFile = a[i + 2];
                }
                wchar_t* end = nullptr;
                const unsigned long asPid = wcstoul(want.c_str(), &end, 10);
                std::vector<vm::ProcInfo> procs = vm::EnumProcesses();
                if (end && *end == 0 && asPid != 0)
                {
                    for (const vm::ProcInfo& p : procs)
                    {
                        if (p.pid == asPid)
                        {
                            nameOut = p.name;
                            return p.pid;
                        }
                    }
                }
                for (const vm::ProcInfo& p : procs)
                {
                    if (StrStrIW(p.name.c_str(), want.c_str()))
                    {
                        nameOut = p.name;
                        return p.pid;
                    }
                }
            }
        }
        return 0;
    }

    // Convert the original MAIN_WINDOW dialog footprint (376x355 dialog units,
    // MS Shell Dlg 8pt) to a first-launch window rect centered on the work
    // area, so the window matches the original before any saved placement.
    void FirstLaunchRect(RECT& rc)
    {
        int bx = 6;
        int by = 13;
        if (HDC hdc = ::GetDC(nullptr))
        {
            const int h = -::MulDiv(8, ::GetDeviceCaps(hdc, LOGPIXELSY), 72);
            HFONT f = ::CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"MS Shell Dlg");
            if (f)
            {
                HGDIOBJ old = ::SelectObject(hdc, f);
                SIZE sz{};
                const wchar_t* alpha = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
                if (::GetTextExtentPoint32W(hdc, alpha, 52, &sz))
                {
                    bx = (sz.cx / 26 + 1) / 2;
                }
                TEXTMETRICW tm{};
                if (::GetTextMetricsW(hdc, &tm))
                {
                    by = tm.tmHeight;
                }
                ::SelectObject(hdc, old);
                ::DeleteObject(f);
            }
            ::ReleaseDC(nullptr, hdc);
        }
        const int cx = ::MulDiv(376, bx, 4);
        const int cy = ::MulDiv(355, by, 8);
        RECT wr = {0, 0, cx, cy};
        ::AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, TRUE, 0);
        const int w = wr.right - wr.left;
        const int hgt = wr.bottom - wr.top;
        RECT work{};
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int x = work.left + ((work.right - work.left) - w) / 2;
        const int y = work.top + ((work.bottom - work.top) - hgt) / 2;
        ::SetRect(&rc, x, y, x + w, y + hgt);
    }

    int RunGui(int nCmdShow, DWORD startupPid, const std::wstring& startupName)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        RECT rc{};
        FirstLaunchRect(rc);
        if (frame.CreateEx(nullptr, &rc) == nullptr)
        {
            return 0;
        }
        frame.ShowWindow(frame.m_havePlacement ? frame.m_placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        if (startupPid != 0)
        {
            frame.TakeSnapshot(startupPid, startupName);
        }

        const int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const std::vector<std::wstring> a = Args();
    const bool acceptFlag = HasFlag(a, L"-accepteula") || HasFlag(a, L"/accepteula");

    if (!Eula::EnsureAccepted(nullptr, acceptFlag))
    {
        return 0;
    }

    std::wstring startupName;
    std::wstring outFile;
    const DWORD startupPid = ResolveTarget(a, startupName, outFile);

    // Headless dump mode: -p <pid> <outputfile> snapshots and exits.
    if (startupPid != 0 && !outFile.empty())
    {
        vm::Snapshot s = vm::Capture(startupPid, startupName);
        return (s.ok && vm::WriteCsv(s, outFile.c_str())) ? 0 : 1;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES |
                                              ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LINK_CLASS};
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"VMMap", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    const int ret = RunGui(nCmdShow, startupPid, startupName);
    _Module.Term();
    return ret;
}
