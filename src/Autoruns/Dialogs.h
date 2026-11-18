// Dialogs.h -- the About box.
//
// The About template carries the recreation credit and the Sysinternals link,
// enlarged from the original's small About dialog to hold the two-line notice.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include "resource.h"

class CAboutDlg : public CDialogImpl<CAboutDlg>
{
  public:
    enum
    {
        IDD = IDD_ABOUT
    };

    BEGIN_MSG_MAP(CAboutDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    NOTIFY_CODE_HANDLER(NM_CLICK, OnLink)
    NOTIFY_CODE_HANDLER(NM_RETURN, OnLink)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About Autoruns");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"Autoruns v14.3");
        // A lone & is an accelerator prefix in a static control, so the
        // ampersand is doubled and renders as a single &.
        SetDlgItemTextW(IDC_ABOUT_DESC, L"Independent source-accurate recreation of Autoruns by Sysinternals, "
                                        L"through reverse engineering and rewritten in modern C++20 && WTL 10.\r\n"
                                        L"Author: 0xntpower");
        return TRUE;
    }

    LRESULT OnLink(int, LPNMHDR p, BOOL&)
    {
        if (p->idFrom == IDC_ABOUT_LINK)
        {
            ::ShellExecuteW(m_hWnd, L"open", L"https://www.sysinternals.com", nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};
