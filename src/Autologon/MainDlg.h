// MainDlg.h -- WTL dialogs for the Autologon recreation. CMainDialog mirrors the
// AUTOLOG template and the WM_INITDIALOG / WM_COMMAND behaviour of sub_140003020.
// CAboutDialog carries the recreation credit.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "resource.h"
#include "AutoLogon.h"

class CAboutDialog : public CDialogImpl<CAboutDialog>
{
public:
    enum { IDD = IDD_ABOUT };

    BEGIN_MSG_MAP(CAboutDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnClose)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About Autologon");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"Autologon v3.1");
        SetDlgItemTextW(IDC_ABOUT_DESC,
            L"Independent source-accurate recreation of Autologon by Sysinternals, "
            L"through reverse engineering and rewritten in modern C++20 && WTL 10.\r\n"
            L"Author: 0xntpower");
        return TRUE;
    }

    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

class CMainDialog : public CDialogImpl<CMainDialog>
{
public:
    enum { IDD = IDD_MAIN };

    BEGIN_MSG_MAP(CMainDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_CLOSE(OnCloseWindow)
        COMMAND_ID_HANDLER(IDOK, OnEnable)
        COMMAND_ID_HANDLER(IDCANCEL, OnDisable)
        COMMAND_ID_HANDLER(IDC_ABOUTBTN, OnAbout)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Autologon - Sysinternals");
        CenterOnScreen();

        const autologon::CurrentState s = autologon::ReadCurrentState();
        if (!s.keyOpened)
        {
            MessageBoxW(L"Could not open Winlogon registry key", L"Autologon", MB_ICONERROR);
        }
        SetDlgItemTextW(IDC_USERNAME, s.user.c_str());
        SetDlgItemTextW(IDC_DOMAIN, s.domain.c_str());
        SetDlgItemTextW(IDC_PASSWORD, s.password.c_str());
        return TRUE;
    }

    void OnCloseWindow()
    {
        EndDialog(IDCANCEL);
    }

    // Enable button (IDOK). SECURITY: a click here writes a real autologon when the
    // fields hold real credentials. The password buffer is wiped after use.
    LRESULT OnEnable(WORD, WORD, HWND, BOOL&)
    {
        wchar_t user[autologon::kFieldChars] = {};
        wchar_t domain[autologon::kFieldChars] = {};
        wchar_t password[autologon::kFieldChars] = {};
        GetDlgItemTextW(IDC_USERNAME, user, autologon::kFieldChars);
        GetDlgItemTextW(IDC_DOMAIN, domain, autologon::kFieldChars);
        GetDlgItemTextW(IDC_PASSWORD, password, autologon::kFieldChars);

        if (password[0] == L'\0')
        {
            MessageBoxW(L"AutoLogon does not support NULL passwords.", L"Autologon", MB_ICONERROR);
            return 0;
        }

        const HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        const autologon::EnableResult r = autologon::Enable(user, domain, password);
        ::SetCursor(prev);
        ::SecureZeroMemory(password, sizeof(password));

        using autologon::EnableResult;
        switch (r)
        {
        case EnableResult::InvalidCredentials:
            MessageBoxW(L"The user name or password is invalid", L"Autologon", MB_ICONERROR);
            return 0;
        case EnableResult::SuccessEncrypted:
            MessageBoxW(L"Autologon successfully configured.\n\nNote: The autologon password is encrypted",
                        L"AutoLogon", MB_ICONINFORMATION);
            EndDialog(IDOK);
            return 0;
        case EnableResult::SuccessPlaintext:
            MessageBoxW(L"Autologon successfully configured.\n\nNote: The autologon password is NOT encrypted",
                        L"AutoLogon", MB_ICONWARNING);
            EndDialog(IDOK);
            return 0;
        case EnableResult::OpenKeyFailed:
            MessageBoxW(L"Could not open Winlogon registry key", L"Autologon", MB_ICONERROR);
            break;
        case EnableResult::WriteUserFailed:
            MessageBoxW(L"Could not write DefaultUserName", L"Autologon", MB_ICONERROR);
            break;
        case EnableResult::WriteDomainFailed:
            MessageBoxW(L"Could not write DefaultDomain", L"Autologon", MB_ICONERROR);
            break;
        case EnableResult::WritePasswordFailed:
            MessageBoxW(L"Could not write DefaultPassword", L"Autologon", MB_ICONERROR);
            break;
        case EnableResult::SetAutoAdminFailed:
            MessageBoxW(L"Could not reset AutoAdminLogon", L"Autologon", MB_ICONERROR);
            break;
        }
        EndDialog(IDCANCEL);
        return 0;
    }

    // Disable button (IDCANCEL).
    LRESULT OnDisable(WORD, WORD, HWND, BOOL&)
    {
        const autologon::DisableResult r = autologon::Disable();
        using autologon::DisableResult;
        switch (r)
        {
        case DisableResult::Success:
            MessageBoxW(L"AutoLogon is disabled.", L"Autologon", MB_ICONINFORMATION);
            break;
        case DisableResult::OpenKeyFailed:
            MessageBoxW(L"Could not open Winlogon registry key", L"Autologon", MB_ICONERROR);
            break;
        case DisableResult::SetAutoAdminFailed:
            MessageBoxW(L"Could not reset AutoAdminLogon", L"Autologon", MB_ICONERROR);
            break;
        }
        EndDialog(IDCANCEL);
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&)
    {
        CAboutDialog().DoModal(m_hWnd);
        return 0;
    }

private:
    // Centre on the primary display, matching sub_140003020: horizontal position
    // nudged by 4 pixels and aligned down to an 8-pixel boundary.
    void CenterOnScreen()
    {
        CRect rc;
        GetWindowRect(&rc);
        const int w = rc.Width();
        const int h = rc.Height();
        const int sw = ::GetSystemMetrics(SM_CXSCREEN);
        const int sh = ::GetSystemMetrics(SM_CYSCREEN);
        const int x = ((sw - w) / 2 + 4) & ~7;
        const int y = (sh - h) / 2;
        MoveWindow(x, y, w, h, FALSE);
    }
};
