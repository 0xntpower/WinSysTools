// ADExplorer.cpp -- entry point, EULA gate and GUI bootstrap for the Active
// Directory Explorer recreation (a source-accurate AD and LDAP browser, x64).
//
// The original gates the run on the Sysinternals license. Acceptance is checked,
// in order, under HKLM\Software\Sysinternals, HKCU\Software\Sysinternals and
// HKCU\Software\Sysinternals\Active Directory Explorer, value EulaAccepted
// (REG_DWORD), recovered from sub_140001F90 in ADExplorer64.exe. A set flag or
// the -accepteula switch records consent under the per-tool key without prompting.
// Window placement and settings live under HKCU\Software\MSDART\Active Directory
// Explorer in the original, stored in an .ini beside the exe here.
//
// Command line (original usage string):
//   adexplorer [-snapshot <connection-string> <snapshot-file>] [-noconnectprompt]
// Snapshots are out of scope, so -snapshot is accepted and reported as such. The
// recreation also honours -accepteula and -noconnectprompt.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include "resource.h"
#include "AdClient.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    constexpr wchar_t kSuiteKey[] = L"Software\\Sysinternals";
    constexpr wchar_t kAppKey[] = L"Software\\Sysinternals\\Active Directory Explorer";
    constexpr wchar_t kEulaValue[] = L"EulaAccepted";

    std::vector<std::wstring> Args()
    {
        std::vector<std::wstring> v;
        int n = 0;
        if (LPWSTR* a = ::CommandLineToArgvW(::GetCommandLineW(), &n))
        {
            for (int i = 1; i < n; ++i)
                v.emplace_back(a[i]);
            ::LocalFree(a);
        }
        return v;
    }

    bool HasFlag(const std::vector<std::wstring>& a, const wchar_t* flag)
    {
        for (const std::wstring& s : a)
            if (_wcsicmp(s.c_str(), flag) == 0)
                return true;
        return false;
    }

    bool EulaFlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
            return false;
        DWORD data = 0, cb = sizeof(data);
        const LSTATUS st = ::RegQueryValueExW(key, kEulaValue, nullptr, nullptr,
                                              reinterpret_cast<LPBYTE>(&data), &cb);
        ::RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    bool EulaAccepted()
    {
        return EulaFlagSet(HKEY_LOCAL_MACHINE, kSuiteKey)
            || EulaFlagSet(HKEY_CURRENT_USER, kSuiteKey)
            || EulaFlagSet(HKEY_CURRENT_USER, kAppKey);
    }

    void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kAppKey, 0, nullptr, 0,
                              KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            ::RegSetValueExW(key, kEulaValue, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&one), sizeof(one));
            ::RegCloseKey(key);
        }
    }

    bool AcceptEula(bool acceptFlag)
    {
        if (acceptFlag || EulaAccepted())
        {
            WriteEulaAccepted();
            return true;
        }
        const int r = ::MessageBoxW(nullptr,
            L"Active Directory Explorer license terms.\r\n\r\n"
            L"This is an independent, source-accurate recreation of the Sysinternals "
            L"Active Directory Explorer utility. It browses and reads an Active "
            L"Directory or LDAP directory.\r\n\r\n"
            L"You can also use the /accepteula command-line switch to accept the terms.\r\n\r\n"
            L"Do you accept the license terms?",
            L"Active Directory Explorer License Agreement", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES)
        {
            WriteEulaAccepted();
            return true;
        }
        return false;
    }

    // Convert the original MAIN dialog footprint (237x173 dialog units, MS Shell
    // Dlg 8pt) to a first-launch window rect centered on the work area, so the
    // window matches the original before any saved placement.
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
                    bx = (sz.cx / 26 + 1) / 2;
                TEXTMETRICW tm{};
                if (::GetTextMetricsW(hdc, &tm))
                    by = tm.tmHeight;
                ::SelectObject(hdc, old);
                ::DeleteObject(f);
            }
            ::ReleaseDC(nullptr, hdc);
        }
        const int cx = ::MulDiv(237, bx, 4);
        const int cy = ::MulDiv(173, by, 8);
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

    int RunGui(int nCmdShow, bool promptConnect)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        RECT rc{};
        FirstLaunchRect(rc);
        if (frame.CreateEx(nullptr, &rc) == nullptr)
            return 0;
        frame.ShowWindow(frame.m_havePlacement ? frame.m_placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        if (promptConnect)
            frame.PostMessage(WM_COMMAND, MAKEWPARAM(ID_FILE_CONNECT, 0), 0);

        const int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const std::vector<std::wstring> a = Args();
    const bool acceptFlag = HasFlag(a, L"-accepteula") || HasFlag(a, L"/accepteula");
    const bool noConnectPrompt = HasFlag(a, L"-noconnectprompt") || HasFlag(a, L"/noconnectprompt");

    if (!AcceptEula(acceptFlag))
        return 0;

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
        ICC_TREEVIEW_CLASSES | ICC_LINK_CLASS };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Active Directory Explorer", MB_ICONERROR);
        ::CoUninitialize();
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    const int ret = RunGui(nCmdShow, !noConnectPrompt);
    _Module.Term();
    ::CoUninitialize();
    return ret;
}
