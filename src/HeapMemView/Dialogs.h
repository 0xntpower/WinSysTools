// Dialogs.h -- the recreation's dialogs over the original extracted templates:
// Select Process (109), Filter (111), Advanced Options (113), Column Settings
// (1096), Properties (105) and About (112).
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atlddx.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "HeapBlock.h"
#include "ProcessList.h"

// ----------------------------------------------------------- Select Process (109)
class CSelectProcessDlg : public CDialogImpl<CSelectProcessDlg>
{
  public:
    enum
    {
        IDD = IDD_SELECT_PROCESS
    };

    CListViewCtrl m_list;
    std::vector<ProcessInfo> m_procs;
    DWORD m_pid = 0;        // out: chosen process id
    DWORD m_initialPid = 0; // in: process to pre-select
    int m_sortCol = 0;
    bool m_sortAsc = true;

    BEGIN_MSG_MAP(CSelectProcessDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    NOTIFY_HANDLER(IDC_PROC_LIST, LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_HANDLER(IDC_PROC_LIST, NM_DBLCLK, OnDblClk)
    COMMAND_ID_HANDLER(IDOK, OnOk)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Select Process");
        m_list.Attach(GetDlgItem(IDC_PROC_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Process ID", LVCFMT_LEFT, 80);
        m_list.InsertColumn(1, L"Process Name", LVCFMT_LEFT, 150);
        m_list.InsertColumn(2, L"Process Path", LVCFMT_LEFT, 240);
        Fill();
        CenterWindow();
        return TRUE;
    }

    void Fill()
    {
        m_procs = EnumerateProcesses();
        Sort();
        m_list.DeleteAllItems();
        for (size_t i = 0; i < m_procs.size(); ++i)
        {
            wchar_t pid[16];
            swprintf(pid, 16, L"%u", m_procs[i].pid);
            int row = m_list.InsertItem((int)i, pid);
            m_list.SetItemText(row, 1, m_procs[i].name.c_str());
            m_list.SetItemText(row, 2, m_procs[i].path.c_str());
            m_list.SetItemData(row, m_procs[i].pid);
            if (m_procs[i].pid == m_initialPid)
            {
                m_list.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
        }
    }

    void Sort()
    {
        const int col = m_sortCol;
        const bool asc = m_sortAsc;
        std::stable_sort(m_procs.begin(), m_procs.end(),
                         [col, asc](const ProcessInfo& a, const ProcessInfo& b)
                         {
                             int c = 0;
                             if (col == 0)
                             {
                                 c = (a.pid > b.pid) - (a.pid < b.pid);
                             }
                             else if (col == 1)
                             {
                                 c = _wcsicmp(a.name.c_str(), b.name.c_str());
                             }
                             else
                             {
                                 c = _wcsicmp(a.path.c_str(), b.path.c_str());
                             }
                             return asc ? (c < 0) : (c > 0);
                         });
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        int c = reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem;
        if (c == m_sortCol)
        {
            m_sortAsc = !m_sortAsc;
        }
        else
        {
            m_sortCol = c;
            m_sortAsc = true;
        }
        Fill();
        return 0;
    }

    void Commit()
    {
        int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
        m_pid = (sel >= 0) ? static_cast<DWORD>(m_list.GetItemData(sel)) : 0;
    }

    LRESULT OnDblClk(int, LPNMHDR, BOOL&)
    {
        Commit();
        if (m_pid)
        {
            EndDialog(IDOK);
        }
        return 0;
    }
    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        EndDialog(m_pid ? IDOK : IDCANCEL);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};

// ------------------------------------------------------------------- Filter (111)
class CFilterDlg : public CDialogImpl<CFilterDlg>
{
  public:
    enum
    {
        IDD = IDD_FILTER
    };
    Settings& m_s;
    explicit CFilterDlg(Settings& s)
        : m_s(s)
    {
    }

    BEGIN_MSG_MAP(CFilterDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOk)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Filter");
        CheckDlgButton(IDC_FILTER_USEFROM, m_s.filterUseFrom ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_FILTER_USETO, m_s.filterUseTo ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(IDC_FILTER_FROM, m_s.filterFromSize, FALSE);
        SetDlgItemInt(IDC_FILTER_TO, m_s.filterToSize, FALSE);
        CenterWindow();
        return TRUE;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        m_s.filterUseFrom = IsDlgButtonChecked(IDC_FILTER_USEFROM) == BST_CHECKED;
        m_s.filterUseTo = IsDlgButtonChecked(IDC_FILTER_USETO) == BST_CHECKED;
        m_s.filterFromSize = GetDlgItemInt(IDC_FILTER_FROM, nullptr, FALSE);
        m_s.filterToSize = GetDlgItemInt(IDC_FILTER_TO, nullptr, FALSE);
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};

// --------------------------------------------------------- Advanced Options (113)
class CAdvancedDlg : public CDialogImpl<CAdvancedDlg>
{
  public:
    enum
    {
        IDD = IDD_ADVANCED
    };
    Settings& m_s;
    explicit CAdvancedDlg(Settings& s)
        : m_s(s)
    {
    }

    BEGIN_MSG_MAP(CAdvancedDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOk)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Advanced Options");
        SetDlgItemInt(IDC_ADV_BYTES, m_s.dataPreviewBytes, FALSE);
        SetDlgItemInt(IDC_ADV_STARTBYTE, m_s.dataPreviewStartByte, FALSE);
        SetDlgItemInt(IDC_ADV_PERLINE, m_s.bytesPerLine, FALSE);
        CheckDlgButton(IDC_ADV_SHOWADDR, m_s.displayMemoryAddress ? BST_CHECKED : BST_UNCHECKED);
        CenterWindow();
        return TRUE;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        m_s.dataPreviewBytes = GetDlgItemInt(IDC_ADV_BYTES, nullptr, FALSE);
        m_s.dataPreviewStartByte = GetDlgItemInt(IDC_ADV_STARTBYTE, nullptr, FALSE);
        m_s.bytesPerLine = GetDlgItemInt(IDC_ADV_PERLINE, nullptr, FALSE);
        if (m_s.bytesPerLine < 1)
        {
            m_s.bytesPerLine = 16;
        }
        m_s.displayMemoryAddress = IsDlgButtonChecked(IDC_ADV_SHOWADDR) == BST_CHECKED;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};

// --------------------------------------------------------- Column Settings (1096)
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
  public:
    enum
    {
        IDD = IDD_COLUMNS
    };
    std::vector<int> m_visible; // in/out
    std::vector<std::pair<int, bool>> m_order;
    CListViewCtrl m_cols;

    explicit CColumnsDlg(const std::vector<int>& visible)
        : m_visible(visible)
    {
    }

    BEGIN_MSG_MAP(CColumnsDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOk)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    COMMAND_ID_HANDLER(IDC_COL_MOVEUP, OnMoveUp)
    COMMAND_ID_HANDLER(IDC_COL_MOVEDOWN, OnMoveDown)
    COMMAND_ID_HANDLER(IDC_COL_SHOW, OnShow)
    COMMAND_ID_HANDLER(IDC_COL_HIDE, OnHide)
    COMMAND_ID_HANDLER(IDC_COL_DEFAULT, OnDefault)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Column Settings");
        m_cols.Attach(GetDlgItem(IDC_COL_LIST));
        m_cols.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        m_cols.InsertColumn(0, L"Column", LVCFMT_LEFT, 140);
        for (int c : m_visible)
        {
            m_order.emplace_back(c, true);
        }
        for (int c = 0; c < kColumnCount; ++c)
        {
            if (std::find(m_visible.begin(), m_visible.end(), c) == m_visible.end())
            {
                m_order.emplace_back(c, false);
            }
        }
        Fill();
        return TRUE;
    }

    void Fill()
    {
        m_cols.DeleteAllItems();
        for (size_t i = 0; i < m_order.size(); ++i)
        {
            m_cols.InsertItem((int)i, kColumns[m_order[i].first].title);
            m_cols.SetCheckState((int)i, m_order[i].second);
        }
    }
    void Commit()
    {
        for (size_t i = 0; i < m_order.size(); ++i)
        {
            m_order[i].second = m_cols.GetCheckState((int)i) != FALSE;
        }
    }
    int Sel()
    {
        return m_cols.GetNextItem(-1, LVNI_SELECTED);
    }

    LRESULT OnMoveUp(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        int s = Sel();
        if (s > 0)
        {
            std::swap(m_order[s], m_order[s - 1]);
            Fill();
            m_cols.SelectItem(s - 1);
        }
        return 0;
    }
    LRESULT OnMoveDown(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        int s = Sel();
        if (s >= 0 && s + 1 < (int)m_order.size())
        {
            std::swap(m_order[s], m_order[s + 1]);
            Fill();
            m_cols.SelectItem(s + 1);
        }
        return 0;
    }
    LRESULT OnShow(WORD, WORD, HWND, BOOL&)
    {
        int s = Sel();
        if (s >= 0)
        {
            m_cols.SetCheckState(s, TRUE);
        }
        return 0;
    }
    LRESULT OnHide(WORD, WORD, HWND, BOOL&)
    {
        int s = Sel();
        if (s >= 0)
        {
            m_cols.SetCheckState(s, FALSE);
        }
        return 0;
    }
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_order.clear();
        for (int c = 0; c < kColumnCount; ++c)
        {
            m_order.emplace_back(c, true);
        }
        Fill();
        return 0;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        m_visible.clear();
        for (auto& [col, vis] : m_order)
        {
            if (vis)
            {
                m_visible.push_back(col);
            }
        }
        if (m_visible.empty())
        {
            m_visible.push_back(0);
        }
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};

// --------------------------------------------------------------- Properties (105)
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>
{
  public:
    enum
    {
        IDD = IDD_PROPERTIES
    };
    const HeapBlock& m_item;
    std::vector<int> m_cols;

    CPropertiesDlg(const HeapBlock& item, const std::vector<int>& cols)
        : m_item(item)
        , m_cols(cols)
    {
    }

    BEGIN_MSG_MAP(CPropertiesDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Properties");
        HWND hLabel = GetDlgItem(IDC_PROP_LABEL);
        HWND hEdit = GetDlgItem(IDC_PROP_EDIT);
        HWND hOk = GetDlgItem(IDOK);
        HFONT font = (HFONT)::SendMessageW(m_hWnd, WM_GETFONT, 0, 0);

        RECT rl, re, ro, rc;
        ::GetWindowRect(hLabel, &rl);
        ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rl, 2);
        ::GetWindowRect(hEdit, &re);
        ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&re, 2);
        ::GetWindowRect(hOk, &ro);
        ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&ro, 2);
        GetClientRect(&rc);

        int rowH = (re.bottom - re.top) + 6;
        ::ShowWindow(hLabel, SW_HIDE);
        ::ShowWindow(hEdit, SW_HIDE);

        int y = rl.top;
        for (int c : m_cols)
        {
            ::CreateWindowExW(0, L"STATIC", (std::wstring(kColumns[c].title) + L":").c_str(), WS_CHILD | WS_VISIBLE,
                              rl.left, y + 2, re.left - rl.left - 4, rl.bottom - rl.top, m_hWnd, nullptr,
                              ModuleHelper::GetModuleInstance(), nullptr);
            HWND ed =
                ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m_item.column(c).c_str(),
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, re.left, y, re.right - re.left,
                                  re.bottom - re.top, m_hWnd, nullptr, ModuleHelper::GetModuleInstance(), nullptr);
            HWND lab = ::GetWindow(ed, GW_HWNDPREV);
            ::SendMessageW(lab, WM_SETFONT, (WPARAM)font, TRUE);
            ::SendMessageW(ed, WM_SETFONT, (WPARAM)font, TRUE);
            y += rowH;
        }

        int okW = ro.right - ro.left, okH = ro.bottom - ro.top;
        ::SetWindowPos(hOk, nullptr, (rc.right - okW) / 2, y + 8, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        RECT wr;
        GetWindowRect(&wr);
        int extra = (y + 8 + okH + 36) - (rc.bottom - rc.top);
        ::SetWindowPos(m_hWnd, nullptr, 0, 0, wr.right - wr.left, (wr.bottom - wr.top) + extra,
                       SWP_NOMOVE | SWP_NOZORDER);
        CenterWindow();
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// -------------------------------------------------------------------- About (112)
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
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"About HeapMemView");
        SetDlgItemTextW(IDC_ABOUT_TEXT, L"HeapMemView v1.05");
        SetDlgItemTextW(IDC_ABOUT_SUB,
                        L"Independent source-accurate recreation of HeapMemView by NirSoft, through reverse "
                        L"engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        CenterWindow();
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};
