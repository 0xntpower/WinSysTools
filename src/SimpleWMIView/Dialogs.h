// Dialogs.h -- About, Properties and Column Settings dialogs, reusing the
// original's dialog templates (112, 105, 1096).
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atlframe.h>
#include <string>
#include <vector>
#include "resource.h"
#include "WmiResult.h"

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
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About SimpleWMIView");
        SetDlgItemTextW(IDC_ABOUT_TITLE, L"SimpleWMIView v1.56");
        SetDlgItemTextW(IDC_ABOUT_COPY,
                        L"Independent source-accurate recreation of SimpleWMIView by NirSoft, through reverse "
                        L"engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Properties (one record, property / value list) ----------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>, public CDialogResize<CPropertiesDlg>
{
  public:
    enum
    {
        IDD = IDD_PROPERTIES
    };

    const WmiResult& m_res;
    const std::vector<int>& m_cols;
    DisplayOptions m_disp;
    int m_row;
    CListViewCtrl m_list;

    CPropertiesDlg(const WmiResult& res, const std::vector<int>& cols, DisplayOptions d, int row)
        : m_res(res)
        , m_cols(cols)
        , m_disp(d)
        , m_row(row)
    {
    }

    BEGIN_DLGRESIZE_MAP(CPropertiesDlg)
    DLGRESIZE_CONTROL(IDC_PROP_VALUE, DLSZ_SIZE_X | DLSZ_SIZE_Y)
    DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_X | DLSZ_MOVE_Y)
    DLGRESIZE_CONTROL(IDC_PROP_NEXT, DLSZ_MOVE_X | DLSZ_MOVE_Y)
    DLGRESIZE_CONTROL(IDC_PROP_PREV, DLSZ_MOVE_X | DLSZ_MOVE_Y)
    END_DLGRESIZE_MAP()

    BEGIN_MSG_MAP(CPropertiesDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    COMMAND_ID_HANDLER(IDC_PROP_NEXT, OnNext)
    COMMAND_ID_HANDLER(IDC_PROP_PREV, OnPrev)
    CHAIN_MSG_MAP(CDialogResize<CPropertiesDlg>)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Properties");
        if (CWindow name = GetDlgItem(IDC_PROP_NAME))
        {
            name.ShowWindow(SW_HIDE);
        }
        if (CWindow edit = GetDlgItem(IDC_PROP_VALUE))
        {
            edit.ShowWindow(SW_HIDE);
        }

        CRect rc;
        GetClientRect(&rc);
        rc.bottom -= 24;
        m_list.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE,
                      IDC_PROP_VALUE + 100);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Property", LVCFMT_LEFT, 170);
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 380);
        Fill();
        DlgResize_Init(true, true);
        return TRUE;
    }

    void Fill()
    {
        m_list.DeleteAllItems();
        if (m_row < 0 || m_row >= (int)m_res.rows.size())
        {
            return;
        }
        for (int c : m_cols)
        {
            int r = m_list.InsertItem(m_list.GetItemCount(), m_res.columns[c].name.c_str());
            m_list.SetItemText(r, 1, m_res.rows[m_row].cells[c].text(m_disp).c_str());
        }
    }

    LRESULT OnNext(WORD, WORD, HWND, BOOL&)
    {
        if (m_row + 1 < (int)m_res.rows.size())
        {
            ++m_row;
            Fill();
        }
        return 0;
    }
    LRESULT OnPrev(WORD, WORD, HWND, BOOL&)
    {
        if (m_row > 0)
        {
            --m_row;
            Fill();
        }
        return 0;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Column Settings -----------------------------------------------------
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
  public:
    enum
    {
        IDD = IDD_COLUMNS
    };

    const WmiResult& m_res;
    std::vector<int>& m_visible; // in/out: column indices in display order
    CListViewCtrl m_list;

    CColumnsDlg(const WmiResult& res, std::vector<int>& vis)
        : m_res(res)
        , m_visible(vis)
    {
    }

    BEGIN_MSG_MAP(CColumnsDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_COL_MOVEUP, OnMoveUp)
    COMMAND_ID_HANDLER(IDC_COL_MOVEDOWN, OnMoveDown)
    COMMAND_ID_HANDLER(IDC_COL_SHOW, OnShow)
    COMMAND_ID_HANDLER(IDC_COL_HIDE, OnHide)
    COMMAND_ID_HANDLER(IDC_COL_DEFAULT, OnDefault)
    COMMAND_ID_HANDLER(IDC_COL_SELALL, OnSelAll)
    COMMAND_ID_HANDLER(IDC_COL_DESELALL, OnDeselAll)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_COL_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Column", LVCFMT_LEFT, 140);
        Populate();
        return TRUE;
    }

    void Populate()
    {
        m_list.DeleteAllItems();
        std::vector<bool> shown(m_res.columns.size(), false);
        int row = 0;
        for (int c : m_visible)
        {
            if (c < 0 || c >= (int)m_res.columns.size())
            {
                continue;
            }
            m_list.InsertItem(row, m_res.columns[c].name.c_str());
            m_list.SetItemData(row, c);
            m_list.SetCheckState(row, TRUE);
            shown[c] = true;
            ++row;
        }
        for (int c = 0; c < (int)m_res.columns.size(); ++c)
        {
            if (!shown[c])
            {
                m_list.InsertItem(row, m_res.columns[c].name.c_str());
                m_list.SetItemData(row, c);
                m_list.SetCheckState(row, FALSE);
                ++row;
            }
        }
    }

    int sel()
    {
        return m_list.GetNextItem(-1, LVNI_SELECTED);
    }

    void swapRows(int a, int b)
    {
        wchar_t ta[256], tb[256];
        m_list.GetItemText(a, 0, ta, 256);
        m_list.GetItemText(b, 0, tb, 256);
        DWORD_PTR da = m_list.GetItemData(a), db = m_list.GetItemData(b);
        BOOL ca = m_list.GetCheckState(a), cb = m_list.GetCheckState(b);
        m_list.SetItemText(a, 0, tb);
        m_list.SetItemData(a, db);
        m_list.SetCheckState(a, cb);
        m_list.SetItemText(b, 0, ta);
        m_list.SetItemData(b, da);
        m_list.SetCheckState(b, ca);
    }

    LRESULT OnMoveUp(WORD, WORD, HWND, BOOL&)
    {
        int i = sel();
        if (i > 0)
        {
            swapRows(i, i - 1);
            m_list.SetItemState(i - 1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
    }
    LRESULT OnMoveDown(WORD, WORD, HWND, BOOL&)
    {
        int i = sel();
        if (i >= 0 && i < m_list.GetItemCount() - 1)
        {
            swapRows(i, i + 1);
            m_list.SetItemState(i + 1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
    }
    LRESULT OnShow(WORD, WORD, HWND, BOOL&)
    {
        if (int i = sel(); i >= 0)
        {
            m_list.SetCheckState(i, TRUE);
        }
        return 0;
    }
    LRESULT OnHide(WORD, WORD, HWND, BOOL&)
    {
        if (int i = sel(); i >= 0)
        {
            m_list.SetCheckState(i, FALSE);
        }
        return 0;
    }
    LRESULT OnSelAll(WORD, WORD, HWND, BOOL&)
    {
        for (int r = 0; r < m_list.GetItemCount(); ++r)
        {
            m_list.SetCheckState(r, TRUE);
        }
        return 0;
    }
    LRESULT OnDeselAll(WORD, WORD, HWND, BOOL&)
    {
        for (int r = 0; r < m_list.GetItemCount(); ++r)
        {
            m_list.SetCheckState(r, FALSE);
        }
        return 0;
    }
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_visible.clear();
        for (int c = 0; c < (int)m_res.columns.size(); ++c)
        {
            m_visible.push_back(c);
        }
        Populate();
        return 0;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        std::vector<int> vis;
        for (int r = 0; r < m_list.GetItemCount(); ++r)
        {
            if (m_list.GetCheckState(r))
            {
                vis.push_back((int)m_list.GetItemData(r));
            }
        }
        if (!vis.empty())
        {
            m_visible = vis;
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
