// WinObj.cpp -- entry point, EULA gate and GUI bootstrap for the WinObj
// recreation (a source-accurate Object Manager namespace browser, x64).
//
// The command line carries only -accepteula (both -accepteula and /accepteula
// spellings), which records acceptance without prompting. Acceptance is stored
// under HKCU\Software\Sysinternals\WinObj, value EulaAccepted (REG_DWORD 1),
// exactly as the original does.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "resource.h"
#include "NtObjMgr.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    constexpr wchar_t kAppKey[] = L"Software\\Sysinternals\\WinObj";
    constexpr wchar_t kSuiteKey[] = L"Software\\Sysinternals";
    constexpr wchar_t kEulaValue[] = L"EulaAccepted";

    std::vector<std::wstring> args()
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

    bool hasFlag(const std::vector<std::wstring>& a, const wchar_t* flag)
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

    bool eulaFlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0, cb = sizeof(data), type = 0;
        LSTATUS st = ::RegQueryValueExW(key, kEulaValue, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &cb);
        ::RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    bool eulaAccepted()
    {
        return eulaFlagSet(HKEY_CURRENT_USER, kAppKey) || eulaFlagSet(HKEY_CURRENT_USER, kSuiteKey) ||
               eulaFlagSet(HKEY_LOCAL_MACHINE, kSuiteKey);
    }

    void writeEulaAccepted()
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kAppKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
            ERROR_SUCCESS)
        {
            DWORD one = 1;
            ::RegSetValueExW(key, kEulaValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            ::RegCloseKey(key);
        }
    }

    // Gate the run on the EULA. When -accepteula was passed or the flag is
    // already stored, acceptance is recorded and the run proceeds. Otherwise a
    // prompt offers to accept, mirroring the original's License Agreement
    // dialog in a plain message box.
    bool acceptEula(bool acceptFlag)
    {
        if (acceptFlag || eulaAccepted())
        {
            writeEulaAccepted();
            return true;
        }
        int r = ::MessageBoxW(nullptr,
                              L"WinObj license terms.\r\n\r\n"
                              L"This is an independent, source-accurate recreation of the Sysinternals "
                              L"WinObj utility. It browses the Windows Object Manager namespace.\r\n\r\n"
                              L"You can also use the /accepteula command-line switch to accept the terms.\r\n\r\n"
                              L"Do you accept the license terms?",
                              L"WinObj License Agreement", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES)
        {
            writeEulaAccepted();
            return true;
        }
        return false;
    }

    int runGui(int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        // The original frame is created with the MFC default rectangle, so the
        // first-launch window cascades. Saved placement overrides on later runs.
        if (frame.CreateEx(nullptr) == nullptr)
        {
            return 0;
        }
        frame.ShowWindow(frame.m_havePlacement ? frame.m_placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const auto a = args();
    const bool acceptFlag = hasFlag(a, L"-accepteula") || hasFlag(a, L"/accepteula");

    if (!acceptEula(acceptFlag))
    {
        return 0;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES |
                                              ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LINK_CLASS};
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"WinObj", MB_ICONERROR);
        ::CoUninitialize();
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    int ret = runGui(nCmdShow);
    _Module.Term();
    ::CoUninitialize();
    return ret;
}
