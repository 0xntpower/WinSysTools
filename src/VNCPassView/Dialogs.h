// Dialogs.h -- About, Properties and Column Settings dialogs. All three reuse the
// original extracted templates (112, 105, 1096) for visual parity.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "VncPassword.h"
#include "Columns.h"

// ---------------------------------------------------------------- About (112)
class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
    enum { IDD = IDD_ABOUT };

    BEGIN_MSG_MAP(CAboutDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnClose)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
        COMMAND_ID_HANDLER(IDC_ABOUT_LINK, OnLink)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"About VNCPassView");
        SetDlgItemTextW(IDC_ABOUT_TEXT, L"VNCPassView v1.05");
        SetDlgItemTextW(IDC_ABOUT_LINK, L"Independent source-accurate recreation of VNCPassView by NirSoft, through reverse engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
    LRESULT OnLink(WORD, WORD, HWND, BOOL&)
    {
        return 0;
    }
};

// ------------------------------------------------------- Column Settings (1096)
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
public:
    enum { IDD = IDD_COLUMNS };

    std::vector<int> m_visible;                 // in/out: visible columns in order
    std::vector<std::pair<int, bool>> m_order;  // working list: column index + visible flag
    CListViewCtrl m_cols;

    explicit CColumnsDlg(const std::vector<int>& visible) : m_visible(visible) {}

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
        m_cols.Attach(GetDlgItem(IDC_COL_LIST));
        m_cols.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        m_cols.InsertColumn(0, L"Column", LVCFMT_LEFT, 140);

        for (int c : m_visible) m_order.emplace_back(c, true);
        for (int c = 0; c < kColumnCount; ++c)
            if (std::find(m_visible.begin(), m_visible.end(), c) == m_visible.end())
                m_order.emplace_back(c, false);

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
            m_order[i].second = m_cols.GetCheckState((int)i) != FALSE;
    }

    int Sel() { return m_cols.GetNextItem(-1, LVNI_SELECTED); }

    LRESULT OnMoveUp(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        int s = Sel();
        if (s > 0) { std::swap(m_order[s], m_order[s - 1]); Fill(); m_cols.SelectItem(s - 1); }
        return 0;
    }
    LRESULT OnMoveDown(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        int s = Sel();
        if (s >= 0 && s + 1 < (int)m_order.size()) { std::swap(m_order[s], m_order[s + 1]); Fill(); m_cols.SelectItem(s + 1); }
        return 0;
    }
    LRESULT OnShow(WORD, WORD, HWND, BOOL&) { int s = Sel(); if (s >= 0) m_cols.SetCheckState(s, TRUE); return 0; }
    LRESULT OnHide(WORD, WORD, HWND, BOOL&) { int s = Sel(); if (s >= 0) m_cols.SetCheckState(s, FALSE); return 0; }
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_order.clear();
        for (int c = 0; c < kColumnCount; ++c) m_order.emplace_back(c, true);
        Fill();
        return 0;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        Commit();
        m_visible.clear();
        for (auto& [col, vis] : m_order) if (vis) m_visible.push_back(col);
        if (m_visible.empty()) m_visible.push_back(0);
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ---------------------------------------------------------- Properties (105)
// Template 105 holds a label+edit "row" that the original clones per field.
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    const VncPassword& m_item;
    std::vector<int> m_cols;

    CPropertiesDlg(const VncPassword& item, const std::vector<int>& cols) : m_item(item), m_cols(cols) {}

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
        ::GetWindowRect(hLabel, &rl); ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rl, 2);
        ::GetWindowRect(hEdit, &re);  ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&re, 2);
        ::GetWindowRect(hOk, &ro);    ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&ro, 2);
        GetClientRect(&rc);

        int rowH = (re.bottom - re.top) + 6;
        ::ShowWindow(hLabel, SW_HIDE);
        ::ShowWindow(hEdit, SW_HIDE);

        int y = rl.top;
        for (int c : m_cols)
        {
            HWND lab = ::CreateWindowExW(0, L"STATIC", (std::wstring(kColumns[c].title) + L":").c_str(),
                WS_CHILD | WS_VISIBLE, rl.left, y + 2, re.left - rl.left - 4, rl.bottom - rl.top,
                m_hWnd, nullptr, ModuleHelper::GetModuleInstance(), nullptr);
            HWND ed = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m_item.column(c).c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, re.left, y, re.right - re.left, re.bottom - re.top,
                m_hWnd, nullptr, ModuleHelper::GetModuleInstance(), nullptr);
            ::SendMessageW(lab, WM_SETFONT, (WPARAM)font, TRUE);
            ::SendMessageW(ed, WM_SETFONT, (WPARAM)font, TRUE);
            y += rowH;
        }

        int okW = ro.right - ro.left, okH = ro.bottom - ro.top;
        ::SetWindowPos(hOk, nullptr, (rc.right - okW) / 2, y + 8, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        RECT wr; GetWindowRect(&wr);
        int extra = (y + 8 + okH + 36) - (rc.bottom - rc.top);
        ::SetWindowPos(m_hWnd, nullptr, 0, 0, wr.right - wr.left, (wr.bottom - wr.top) + extra,
                       SWP_NOMOVE | SWP_NOZORDER);
        CenterWindow();
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};
