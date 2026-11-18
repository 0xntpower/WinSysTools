// AdInsight.cpp -- entry point, EULA gate and GUI bootstrap for the AdInsight
// recreation, a source-accurate LDAP (Active Directory) client-call monitor.
//
// The command line carries -accepteula (both -accepteula and /accepteula
// spellings), which records acceptance without prompting under
// HKCU\Software\Sysinternals\ADInsight, value EulaAccepted, matching the
// original. A headless -inject <pid> path extracts the embedded hook DLL and
// places it into a target process by id, used only against the benign test
// process spawned for validation. The original also enforces a single instance
// of ADInsight.EXE, reproduced here.
#include <winsock2.h>
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <psapi.h>
#include <string>
#include <vector>
#include "resource.h"
#include "MainFrame.h"
#include "Eula.h"
#include "Injector.h"

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

    bool IsFlag(const std::wstring& s, const wchar_t* name)
    {
        return s.size() > 1 && (s[0] == L'-' || s[0] == L'/') && _wcsicmp(s.c_str() + 1, name) == 0;
    }

    bool HasFlag(const std::vector<std::wstring>& a, const wchar_t* name)
    {
        for (const std::wstring& s : a)
        {
            if (IsFlag(s, name))
            {
                return true;
            }
        }
        return false;
    }

    // Headless injection path used by the validation harness. Extracts the hook
    // DLL and places it into the given PID, then exits.
    int RunInject(DWORD pid)
    {
        const std::wstring dll = adin::ExtractHookDll();
        if (dll.empty())
        {
            return 2;
        }
        return adin::InjectIntoPid(pid, dll) ? 0 : 3;
    }

    int CountSelfInstances()
    {
        DWORD ids[1024] = {};
        DWORD needed = 0;
        if (!::K32EnumProcesses(ids, sizeof(ids), &needed))
        {
            return 1;
        }
        int count = 0;
        const int n = static_cast<int>(needed / sizeof(DWORD));
        for (int i = 0; i < n; ++i)
        {
            if (ids[i] == 0)
            {
                continue;
            }
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ids[i]);
            if (h == nullptr)
            {
                continue;
            }
            wchar_t name[MAX_PATH] = {};
            DWORD cch = MAX_PATH;
            if (::QueryFullProcessImageNameW(h, 0, name, &cch))
            {
                const wchar_t* base = name;
                for (const wchar_t* p = name; *p != 0; ++p)
                {
                    if (*p == L'\\')
                    {
                        base = p + 1;
                    }
                }
                if (_wcsicmp(base, L"AdInsight.exe") == 0)
                {
                    ++count;
                }
            }
            ::CloseHandle(h);
        }
        return count;
    }

    int RunGui(int nCmdShow, bool startCapture)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_startCapture = startCapture;
        // The original creates its main window as WS_OVERLAPPEDWINDOW at the
        // saved placement, defaulting to the system position on first run. The
        // WTL frame traits supply that style.
        if (frame.CreateEx() == nullptr)
        {
            return 0;
        }
        frame.ShowWindow(frame.m_havePlacement ? frame.m_placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        const int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const std::vector<std::wstring> a = Args();

    // Headless injection path (validation use only): -inject <pid>.
    for (size_t i = 0; i + 1 < a.size(); ++i)
    {
        if (IsFlag(a[i], L"inject"))
        {
            return RunInject(static_cast<DWORD>(_wtoi(a[i + 1].c_str())));
        }
    }

    const bool acceptFlag = HasFlag(a, L"accepteula");
    if (!Eula::EnsureAccepted(nullptr, acceptFlag))
    {
        return 0;
    }

    if (CountSelfInstances() > 1)
    {
        ::MessageBoxW(nullptr,
                      L"Only one instance of Insight for Active Directory is allowed.\n\n"
                      L"To open a new window choose 'New Window' from the File menu.",
                      L"Cannot continue", MB_ICONWARNING);
        return 0;
    }

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES |
                                                 ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS};
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"AdInsight", MB_ICONERROR);
        return 1;
    }

    WSADATA wsa = {};
    ::WSAStartup(0x0202, &wsa); // matches the original

    const bool captureOff = HasFlag(a, L"o"); // -o turns off event capture

    _Module.Init(nullptr, hInstance);
    const int ret = RunGui(nCmdShow, !captureOff);
    _Module.Term();
    return ret;
}
