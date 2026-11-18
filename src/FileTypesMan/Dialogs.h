// Dialogs.h -- About, Properties and Column Settings dialogs. All three reuse the
// original extracted templates (112, 105, 1096) for visual parity. The Properties and
// Column dialogs are generic so both the types pane and the actions pane use them.
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
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"About FileTypesMan");
        SetDlgItemTextW(IDC_ABOUT_TEXT, L"FileTypesMan v2.01");
        SetDlgItemTextW(IDC_ABOUT_SUB, L"Independent source-accurate recreation of FileTypesMan by NirSoft, through reverse engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        if (CWindow t1 = GetDlgItem(1005)) t1.ShowWindow(SW_HIDE);
        if (CWindow t2 = GetDlgItem(1006)) t2.ShowWindow(SW_HIDE);
        CenterWindow();
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ------------------------------------------------------- Column Settings (1096)
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
public:
    enum { IDD = IDD_COLUMNS };

    const ColumnDef* m_defs;
    int              m_count;
    std::vector<int> m_visible;                 // in/out, visible columns in order
    std::vector<std::pair<int, bool>> m_order;  // working list, column index + visible flag
    CListViewCtrl m_cols;

    CColumnsDlg(const ColumnDef* defs, int count, const std::vector<int>& visible)
        : m_defs(defs), m_count(count), m_visible(visible) {}

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
        for (int c = 0; c < m_count; ++c)
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
            m_cols.InsertItem((int)i, m_defs[m_order[i].first].title);
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
        Commit(); int s = Sel();
        if (s > 0) { std::swap(m_order[s], m_order[s - 1]); Fill(); m_cols.SelectItem(s - 1); }
        return 0;
    }
    LRESULT OnMoveDown(WORD, WORD, HWND, BOOL&)
    {
        Commit(); int s = Sel();
        if (s >= 0 && s + 1 < (int)m_order.size()) { std::swap(m_order[s], m_order[s + 1]); Fill(); m_cols.SelectItem(s + 1); }
        return 0;
    }
    LRESULT OnShow(WORD, WORD, HWND, BOOL&) { int s = Sel(); if (s >= 0) m_cols.SetCheckState(s, TRUE); return 0; }
    LRESULT OnHide(WORD, WORD, HWND, BOOL&) { int s = Sel(); if (s >= 0) m_cols.SetCheckState(s, FALSE); return 0; }
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_order.clear();
        for (int c = 0; c < m_count; ++c) m_order.emplace_back(c, true);
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

// ------------------------------------------------------------- Input (105)
// A one-line text prompt that reuses the Properties template (label 1001, edit 1000).
class CInputDlg : public CDialogImpl<CInputDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };
    std::wstring m_prompt, m_text;

    CInputDlg(const wchar_t* prompt, const wchar_t* initial) : m_prompt(prompt), m_text(initial) {}

    BEGIN_MSG_MAP(CInputDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOk)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"FileTypesMan");
        SetDlgItemTextW(IDC_PROP_LABEL, m_prompt.c_str());
        HWND ed = GetDlgItem(IDC_PROP_EDIT);
        ::SetWindowLongW(ed, GWL_STYLE, ::GetWindowLongW(ed, GWL_STYLE) & ~ES_READONLY);
        SetDlgItemTextW(IDC_PROP_EDIT, m_text.c_str());
        CenterWindow();
        return TRUE;
    }
    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        wchar_t buf[512] = {};
        GetDlgItemTextW(IDC_PROP_EDIT, buf, 512);
        m_text = buf;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ---------------------------------------------------------- Properties (105)
// Template 105 holds one label+edit row that the original clones per visible field.
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    std::vector<std::pair<std::wstring, std::wstring>> m_fields;

    explicit CPropertiesDlg(std::vector<std::pair<std::wstring, std::wstring>> fields)
        : m_fields(std::move(fields)) {}

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

        int rowH = (re.bottom - re.top) + 4;
        ::ShowWindow(hLabel, SW_HIDE);
        ::ShowWindow(hEdit, SW_HIDE);

        int y = rl.top;
        for (auto& [title, value] : m_fields)
        {
            ::CreateWindowExW(0, L"STATIC", (title + L":").c_str(),
                WS_CHILD | WS_VISIBLE, rl.left, y + 2, re.left - rl.left - 4, rl.bottom - rl.top,
                m_hWnd, nullptr, ModuleHelper::GetModuleInstance(), nullptr);
            HWND ed = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, re.left, y, re.right - re.left, re.bottom - re.top,
                m_hWnd, nullptr, ModuleHelper::GetModuleInstance(), nullptr);
            HWND lab = ::GetWindow(ed, GW_HWNDPREV);
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
