// Autologon.cpp -- entry point and command-line handling for the Autologon
// recreation (a source-accurate x64 recreation of Sysinternals Autologon).
//
// Modes, recovered from WinMain and sub_140002300 / sub_140001DB0:
//   no arguments                       -- show the configuration dialog
//   <username> <domain> <password>     -- enable autologon silently, no UI
//   anything else                      -- usage message box
//   -accepteula / /accepteula          -- accept the EULA, then continue
//
// SECURITY: the command-line enable mode and the dialog's Enable button both write
// a real autologon when given real credentials. Nothing here enables autologon on
// its own, and no credential is ever synthesized.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "resource.h"
#include "AutoLogon.h"
#include "MainDlg.h"
#include "../common/UiUtil.h"

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

    // Strip every -accepteula / /accepteula token, returning true when one was found.
    bool RemoveAccepteula(std::vector<std::wstring>& a)
    {
        bool found = false;
        for (auto it = a.begin(); it != a.end(); )
        {
            if (_wcsicmp(it->c_str(), L"/accepteula") == 0 || _wcsicmp(it->c_str(), L"-accepteula") == 0)
            {
                found = true;
                it = a.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return found;
    }

    // The original shows the full Sysinternals license in a RichEdit dialog. This
    // recreation presents the acceptance choice and the -accepteula hint, and gates
    // on the same EulaAccepted flag.
    bool PromptEula()
    {
        const int r = ::MessageBoxW(nullptr,
            L"This utility is governed by the Sysinternals Software License Terms.\n\n"
            L"You can also use the -accepteula command-line switch to accept the license.\n\n"
            L"Do you accept the license terms?",
            L"Autologon License Agreement", MB_YESNO | MB_ICONQUESTION);
        return r == IDYES;
    }

    bool EulaGate(std::vector<std::wstring>& args)
    {
        if (RemoveAccepteula(args))
        {
            eula::Accept();
            return true;
        }
        if (eula::IsAccepted())
        {
            return true;
        }
        if (PromptEula())
        {
            eula::Accept();
            return true;
        }
        return false;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    std::vector<std::wstring> args = Args();
    if (!EulaGate(args))
    {
        return 1;
    }

    PinToLightMode();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Autologon", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);

    int ret = 0;
    if (args.empty())
    {
        CMainDialog dlg;
        dlg.DoModal();
    }
    else if (args.size() == 3)
    {
        ret = static_cast<int>(autologon::Enable(args[0].c_str(), args[1].c_str(), args[2].c_str()));
    }
    else
    {
        ::MessageBoxW(nullptr, L"Usage: autologon <username> <domain> <password>", L"Autologon", MB_ICONERROR);
        ret = -1;
    }

    _Module.Term();
    return ret;
}
