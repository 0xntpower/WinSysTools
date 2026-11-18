// Dialogs.h -- About, Properties, Advanced Options and Column Settings dialogs,
// reusing the original's dialog templates (112, 105, 111, 1096).
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <string>
#include <vector>
#include "resource.h"
#include "TaskRecord.h"
#include "Columns.h"
#include "Config.h"

// ---- About ---------------------------------------------------------------
class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
    enum { IDD = IDD_ABOUT };

    BEGIN_MSG_MAP(CAboutDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnClose)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About TaskSchedulerView");
        SetDlgItemTextW(1002, L"TaskSchedulerView v1.74");
        SetDlgItemTextW(1004, L"Copyright (c) 2015 - 2023 Nir Sofer");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Properties (Property / Value list for one record) -------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>, public CDialogResize<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    const TaskRecord& m_rec;
    const std::vector<int>& m_cols;
    CListViewCtrl m_list;

    CPropertiesDlg(const TaskRecord& r, const std::vector<int>& cols) : m_rec(r), m_cols(cols) {}

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
        CHAIN_MSG_MAP(CDialogResize<CPropertiesDlg>)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Properties");
        if (CWindow lbl = GetDlgItem(IDC_PROP_LABEL)) lbl.ShowWindow(SW_HIDE);
        if (CWindow edit = GetDlgItem(IDC_PROP_VALUE)) edit.ShowWindow(SW_HIDE);
        if (CWindow nx = GetDlgItem(IDC_PROP_NEXT)) nx.ShowWindow(SW_HIDE);
        if (CWindow pv = GetDlgItem(IDC_PROP_PREV)) pv.ShowWindow(SW_HIDE);

        CRect rc; GetClientRect(&rc);
        rc.bottom -= 28;
        m_list.Create(m_hWnd, rc, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, IDC_PROP_VALUE + 100);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Property", LVCFMT_LEFT, 170);
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 360);
        for (int c : m_cols)
        {
            int row = m_list.InsertItem(m_list.GetItemCount(), kColumns[c].title);
            m_list.SetItemText(row, 1, m_rec.column(c).c_str());
        }
        DlgResize_Init(true, true);
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Advanced Options (This computer / Remote computer) ------------------
class CAdvancedDlg : public CDialogImpl<CAdvancedDlg>
{
public:
    enum { IDD = IDD_ADVANCED };

    Config& m_cfg;
    explicit CAdvancedDlg(Config& cfg) : m_cfg(cfg) {}

    BEGIN_MSG_MAP(CAdvancedDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        CComboBox combo = GetDlgItem(IDC_ADV_LOADFROM);
        combo.AddString(L"This computer");
        combo.AddString(L"Remote computer");
        combo.SetCurSel(m_cfg.loadFrom == 2 ? 1 : 0);
        SetDlgItemTextW(IDC_ADV_COMPUTER, m_cfg.computerName.c_str());
        CheckDlgButton(IDC_ADV_STARTREMOTE, m_cfg.autoStartRemoteRegistry ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        CComboBox combo = GetDlgItem(IDC_ADV_LOADFROM);
        m_cfg.loadFrom = combo.GetCurSel() == 1 ? 2 : 1;
        wchar_t buf[512]{};
        GetDlgItemTextW(IDC_ADV_COMPUTER, buf, 512);
        m_cfg.computerName = buf;
        m_cfg.autoStartRemoteRegistry = IsDlgButtonChecked(IDC_ADV_STARTREMOTE) == BST_CHECKED;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ---- Column Settings -----------------------------------------------------
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
public:
    enum { IDD = IDD_COLUMNS };

    std::vector<int> m_visible;
    std::vector<int> m_widths;
    CListViewCtrl m_list;

    CColumnsDlg(std::vector<int> vis, std::vector<int> wid)
        : m_visible(std::move(vis)), m_widths(std::move(wid)) {}

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
        std::vector<bool> shown(kColumnCount, false);
        int row = 0;
        for (int c : m_visible)
        {
            m_list.InsertItem(row, kColumns[c].title);
            m_list.SetItemData(row, c);
            m_list.SetCheckState(row, TRUE);
            shown[c] = true;
            ++row;
        }
        for (int c = 0; c < kColumnCount; ++c)
            if (!shown[c])
            {
                m_list.InsertItem(row, kColumns[c].title);
                m_list.SetItemData(row, c);
                m_list.SetCheckState(row, FALSE);
                ++row;
            }
    }

    int sel() { return m_list.GetNextItem(-1, LVNI_SELECTED); }

    void swapRows(int a, int b)
    {
        wchar_t ta[256], tb[256];
        m_list.GetItemText(a, 0, ta, 256);
        m_list.GetItemText(b, 0, tb, 256);
        DWORD_PTR da = m_list.GetItemData(a), db = m_list.GetItemData(b);
        BOOL ca = m_list.GetCheckState(a), cb = m_list.GetCheckState(b);
        m_list.SetItemText(a, 0, tb); m_list.SetItemData(a, db); m_list.SetCheckState(a, cb);
        m_list.SetItemText(b, 0, ta); m_list.SetItemData(b, da); m_list.SetCheckState(b, ca);
    }

    LRESULT OnMoveUp(WORD, WORD, HWND, BOOL&)
    { int i = sel(); if (i > 0) { swapRows(i, i - 1); m_list.SetItemState(i - 1, LVIS_SELECTED, LVIS_SELECTED); } return 0; }
    LRESULT OnMoveDown(WORD, WORD, HWND, BOOL&)
    { int i = sel(); if (i >= 0 && i < m_list.GetItemCount() - 1) { swapRows(i, i + 1); m_list.SetItemState(i + 1, LVIS_SELECTED, LVIS_SELECTED); } return 0; }
    LRESULT OnShow(WORD, WORD, HWND, BOOL&) { if (int i = sel(); i >= 0) m_list.SetCheckState(i, TRUE); return 0; }
    LRESULT OnHide(WORD, WORD, HWND, BOOL&) { if (int i = sel(); i >= 0) m_list.SetCheckState(i, FALSE); return 0; }
    LRESULT OnSelAll(WORD, WORD, HWND, BOOL&) { for (int i = 0; i < m_list.GetItemCount(); ++i) m_list.SetCheckState(i, TRUE); return 0; }
    LRESULT OnDeselAll(WORD, WORD, HWND, BOOL&) { for (int i = 0; i < m_list.GetItemCount(); ++i) m_list.SetCheckState(i, FALSE); return 0; }
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_visible.assign(std::begin(kDefaultOrder), std::end(kDefaultOrder));
        m_widths.clear();
        for (int c : m_visible) m_widths.push_back(kColumns[c].width);
        Populate();
        return 0;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        std::vector<int> vis, wid;
        for (int r = 0; r < m_list.GetItemCount(); ++r)
            if (m_list.GetCheckState(r))
            {
                int c = (int)m_list.GetItemData(r);
                vis.push_back(c);
                wid.push_back(kColumns[c].width);
            }
        if (!vis.empty()) { m_visible = vis; m_widths = wid; }
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};
