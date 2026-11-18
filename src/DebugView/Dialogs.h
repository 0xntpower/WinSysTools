// Dialogs.h -- About, Filter/Highlight and History Depth dialogs.
// About reuses the recreation's own template and carries the required credit.
// Filter and History reuse the original dialog templates, wired to the fields
// this recreation supports.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <string>
#include "resource.h"
#include "Filter.h"

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
    COMMAND_ID_HANDLER(IDC_ABOUT_OK, OnClose)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About DebugView");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"DebugView  (version 4.90)");
        SetDlgItemTextW(IDC_ABOUT_CREDIT, L"Independent source-accurate recreation of DebugView by Sysinternals, "
                                          L"through reverse engineering and rewritten in modern C++20 && WTL 10.");
        SetDlgItemTextW(IDC_ABOUT_AUTHOR, L"Author: 0xntpower");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Filter / Highlight --------------------------------------------------
class CFilterDlg : public CDialogImpl<CFilterDlg>
{
  public:
    enum
    {
        IDD = IDD_FILTER
    };

    Filter m_filter;

    explicit CFilterDlg(const Filter& f)
        : m_filter(f)
    {
    }

    BEGIN_MSG_MAP(CFilterDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_FILTER_RESET, OnReset)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"DebugView Filter");
        SetDlgItemTextW(IDC_FILTER_INCLUDE, m_filter.include.c_str());
        SetDlgItemTextW(IDC_FILTER_EXCLUDE, m_filter.exclude.c_str());
        SetDlgItemTextW(IDC_FILTER_HIGHLIGHT, m_filter.highlight.c_str());
        return TRUE;
    }

    std::wstring field(int id)
    {
        wchar_t buf[1024]{};
        GetDlgItemTextW(id, buf, 1024);
        return buf;
    }

    LRESULT OnReset(WORD, WORD, HWND, BOOL&)
    {
        SetDlgItemTextW(IDC_FILTER_INCLUDE, L"*");
        SetDlgItemTextW(IDC_FILTER_EXCLUDE, L"");
        SetDlgItemTextW(IDC_FILTER_HIGHLIGHT, L"");
        return 0;
    }
    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        m_filter.include = field(IDC_FILTER_INCLUDE);
        if (m_filter.include.empty())
        {
            m_filter.include = L"*";
        }
        m_filter.exclude = field(IDC_FILTER_EXCLUDE);
        m_filter.highlight = field(IDC_FILTER_HIGHLIGHT);
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};

// ---- History Depth -------------------------------------------------------
class CHistoryDlg : public CDialogImpl<CHistoryDlg>
{
  public:
    enum
    {
        IDD = IDD_HISTORY
    };

    int m_depth = 0;

    explicit CHistoryDlg(int depth)
        : m_depth(depth)
    {
    }

    BEGIN_MSG_MAP(CHistoryDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_HISTORY_RESET, OnReset)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"DebugView History Depth");
        if (CUpDownCtrl spin = GetDlgItem(IDC_HISTORY_SPIN); spin.IsWindow())
        {
            spin.SetRange32(0, 100000000);
        }
        SetDlgItemInt(IDC_HISTORY_EDIT, static_cast<UINT>(m_depth), FALSE);
        return TRUE;
    }
    LRESULT OnReset(WORD, WORD, HWND, BOOL&)
    {
        SetDlgItemInt(IDC_HISTORY_EDIT, 0, FALSE);
        return 0;
    }
    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        m_depth = static_cast<int>(GetDlgItemInt(IDC_HISTORY_EDIT, nullptr, FALSE));
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};
