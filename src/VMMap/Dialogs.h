// Dialogs.h -- the About box and the Select Process picker.
//
// About reuses the original template, enlarged to carry the recreation credit.
// The picker lists running processes (name, PID, user, private bytes, working
// set, architecture) and returns the chosen PID. The original wraps this list
// and a "launch and trace" tab in one dialog. The trace tab is out of scope.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <string>
#include <vector>
#include "resource.h"
#include "ProcessList.h"

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
        SetWindowTextW(L"About VMMap");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"VMMap v3.33");
        // A lone & is an accelerator prefix in a static control, so the
        // ampersand is doubled and renders as a single &.
        SetDlgItemTextW(IDC_ABOUT_DESC, L"Independent source-accurate recreation of VMMap by Sysinternals, "
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

// ---- Select Process picker ----------------------------------------------
class CSelectProcessDlg : public CDialogImpl<CSelectProcessDlg>
{
  public:
    enum
    {
        IDD = IDD_SELECT_PROCESS
    };

    DWORD m_pid = 0;
    std::wstring m_name;
    std::vector<vm::ProcInfo> m_procs;
    CListViewCtrl m_list;

    BEGIN_MSG_MAP(CSelectProcessDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    COMMAND_ID_HANDLER(IDC_PROC_REFRESH, OnRefresh)
    NOTIFY_HANDLER(IDC_PROC_LIST, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_PROC_LIST, NM_DBLCLK, OnDblClk)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_PROC_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
        m_list.InsertColumn(0, L"Name", LVCFMT_LEFT, 150);
        m_list.InsertColumn(1, L"PID", LVCFMT_RIGHT, 55);
        m_list.InsertColumn(2, L"User", LVCFMT_LEFT, 150);
        m_list.InsertColumn(3, L"Private Bytes", LVCFMT_RIGHT, 80);
        m_list.InsertColumn(4, L"Working Set", LVCFMT_RIGHT, 80);
        m_list.InsertColumn(5, L"Architecture", LVCFMT_LEFT, 70);
        Populate();
        return TRUE;
    }

    void Populate()
    {
        m_list.SetRedraw(FALSE);
        m_list.DeleteAllItems();
        m_procs = vm::EnumProcesses();
        for (int i = 0; i < static_cast<int>(m_procs.size()); ++i)
        {
            const vm::ProcInfo& p = m_procs[i];
            m_list.InsertItem(i, p.name.c_str());
            wchar_t buf[32] = {};
            swprintf(buf, 32, L"%lu", p.pid);
            m_list.SetItemText(i, 1, buf);
            m_list.SetItemText(i, 2, p.user.c_str());
            m_list.SetItemText(i, 3, FormatKb(p.privateBytes).c_str());
            m_list.SetItemText(i, 4, FormatKb(p.workingSet).c_str());
            m_list.SetItemText(i, 5, p.arch.c_str());
            m_list.SetItemData(i, i);
        }
        m_list.SetRedraw(TRUE);
        ::EnableWindow(GetDlgItem(IDOK), FALSE);
    }

    static std::wstring FormatKb(uint64_t bytes)
    {
        if (bytes == 0)
        {
            return std::wstring();
        }
        const uint64_t kb = (bytes + 1023) / 1024;
        wchar_t raw[32] = {};
        swprintf(raw, 32, L"%llu", static_cast<unsigned long long>(kb));
        std::wstring digits = raw;
        std::wstring grouped;
        int c = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        {
            if (c && c % 3 == 0)
            {
                grouped.push_back(L',');
            }
            grouped.push_back(*it);
            ++c;
        }
        std::reverse(grouped.begin(), grouped.end());
        grouped += L" K";
        return grouped;
    }

    LRESULT OnItemChanged(int, LPNMHDR, BOOL&)
    {
        ::EnableWindow(GetDlgItem(IDOK), m_list.GetSelectedCount() > 0);
        return 0;
    }

    LRESULT OnDblClk(int, LPNMHDR, BOOL&)
    {
        Accept();
        return 0;
    }
    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        Accept();
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&)
    {
        Populate();
        return 0;
    }

    void Accept()
    {
        int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
        if (sel < 0)
        {
            return;
        }
        size_t idx = static_cast<size_t>(m_list.GetItemData(sel));
        if (idx < m_procs.size())
        {
            m_pid = m_procs[idx].pid;
            m_name = m_procs[idx].name;
            EndDialog(IDOK);
        }
    }
};
