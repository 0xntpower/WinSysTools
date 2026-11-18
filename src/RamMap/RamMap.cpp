// RamMap.cpp -- entry point, EULA gate and GUI bootstrap for the RamMap
// recreation, a source-accurate physical-memory analyzer (x64).
//
// The command line carries -accepteula (both -accepteula and /accepteula
// spellings), which records acceptance without prompting. Acceptance is stored
// under HKCU\Software\Sysinternals\RamMap, value EulaAccepted (REG_DWORD 1),
// exactly as the original does. RamMap needs administrative rights and the
// SeProfileSingleProcessPrivilege, SeDebugPrivilege and SeIncreaseQuotaPrivilege
// privileges to read the system memory lists, which WinMain enables up front.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "resource.h"
#include "MainFrame.h"
#include "MemInfo.h"
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

    // Convert the original MAIN_WINDOW dialog footprint (372x287 dialog units,
    // MS Shell Dlg 8pt) to a first-launch window rect centered on the work
    // area, so the window matches the original before any saved placement.
    void FirstLaunchRect(RECT& rc)
    {
        int bx = 6;
        int by = 13;
        if (HDC hdc = ::GetDC(nullptr))
        {
            const int h = -::MulDiv(8, ::GetDeviceCaps(hdc, LOGPIXELSY), 72);
            HFONT f = ::CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"MS Shell Dlg");
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
        const int cx = ::MulDiv(372, bx, 4);
        const int cy = ::MulDiv(287, by, 8);
        RECT wr = { 0, 0, cx, cy };
        ::AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, TRUE, 0);
        const int w = wr.right - wr.left;
        const int hgt = wr.bottom - wr.top;
        RECT work{};
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int x = work.left + ((work.right - work.left) - w) / 2;
        const int y = work.top + ((work.bottom - work.top) - hgt) / 2;
        ::SetRect(&rc, x, y, x + w, y + hgt);
    }

    int RunGui(int nCmdShow)
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

        const int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const std::vector<std::wstring> a = Args();
    const bool acceptFlag = HasFlag(a, L"-accepteula") || HasFlag(a, L"/accepteula");

    if (!Eula::EnsureAccepted(nullptr, acceptFlag))
    {
        return 0;
    }

    // RamMap reads the system memory lists, which need these privileges. The
    // original refuses to run without them.
    if (!ram::EnablePrivileges())
    {
        ::MessageBoxW(nullptr,
            L"Failed to get the required privileges.\r\n"
            L"Run RamMap with administrative rights, with the Profile Single Process, "
            L"Debug and Increase Quota privileges available.",
            L"RamMap", MB_OK | MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
        ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES | ICC_LINK_CLASS };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"RamMap", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    const int ret = RunGui(nCmdShow);
    _Module.Term();
    return ret;
}
