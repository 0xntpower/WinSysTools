// Dialogs.h -- the About box and the small text-input dialog shared by Find
// and the test-process injection prompt.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <shellapi.h>
#include <string>
#include "resource.h"

// ---- About ---------------------------------------------------------------
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
        SetWindowTextW(L"About AdInsight");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"AdInsight v1.2");
        // A lone ampersand is a mnemonic prefix in a static control, so it is
        // doubled to render as a single ampersand.
        SetDlgItemTextW(IDC_ABOUT_DESC, L"Independent source-accurate recreation of AdInsight by Sysinternals, "
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

// ---- single-line text prompt (Find and Inject) ---------------------------
class CFindDlg : public CDialogImpl<CFindDlg>
{
  public:
    enum
    {
        IDD = IDD_FIND
    };

    std::wstring m_prompt;
    std::wstring m_text;

    explicit CFindDlg(const wchar_t* prompt)
        : m_prompt(prompt)
    {
    }

    BEGIN_MSG_MAP(CFindDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOk)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"AdInsight");
        SetDlgItemTextW(IDC_FIND_EDIT, L"");
        SetDlgItemTextW(IDC_FIND_PROMPT, m_prompt.c_str());
        return TRUE;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        wchar_t buf[260] = {};
        GetDlgItemTextW(IDC_FIND_EDIT, buf, 260);
        m_text = buf;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};
