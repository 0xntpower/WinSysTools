// Dialogs.h -- About, Properties, Column Settings and Capture Options, reusing the
// original's dialog templates (112, 105, 1096, 108). The Capture Options dialog reads and
// writes the capture method, promiscuous flag and firewall flag in the Config, and lists
// the machine's adapters through the Capture backend.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <string>
#include <vector>
#include <utility>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "Capture.h"

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
        SetWindowTextW(L"About SniffPass");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"SniffPass v1.13");
        SetDlgItemTextW(IDC_ABOUT_DESC, L"Copyright (c) 2005 - 2014 Nir Sofer");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Properties (Property / Value list for one record) -------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>, public CDialogResize<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    std::vector<std::pair<std::wstring, std::wstring>> m_rows;
    CListViewCtrl m_list;

    explicit CPropertiesDlg(std::vector<std::pair<std::wstring, std::wstring>> rows)
        : m_rows(std::move(rows)) {}

    BEGIN_DLGRESIZE_MAP(CPropertiesDlg)
        DLGRESIZE_CONTROL(IDC_PROP_VALUE, DLSZ_SIZE_X | DLSZ_SIZE_Y)
        DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_X | DLSZ_MOVE_Y)
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

        CRect rc; GetClientRect(&rc);
        rc.bottom -= 28;
        m_list.Create(m_hWnd, rc, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, IDC_PROP_VALUE + 100);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Property", LVCFMT_LEFT, 150);
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 310);
        for (const auto& [name, value] : m_rows)
        {
            int row = m_list.InsertItem(m_list.GetItemCount(), name.c_str());
            m_list.SetItemText(row, 1, value.c_str());
        }
        DlgResize_Init(true, true);
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Column Settings -----------------------------------------------------
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
public:
    enum { IDD = IDD_COLUMNS };

    const ColumnDef* m_defs;
    int m_count;
    ColumnState m_state;
    CListViewCtrl m_list;

    CColumnsDlg(const ColumnDef* defs, int count, ColumnState state)
        : m_defs(defs), m_count(count), m_state(std::move(state)) {}

    BEGIN_MSG_MAP(CColumnsDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDC_COL_MOVEUP, OnMoveUp)
        COMMAND_ID_HANDLER(IDC_COL_MOVEDOWN, OnMoveDown)
        COMMAND_ID_HANDLER(IDC_COL_SHOW, OnShow)
        COMMAND_ID_HANDLER(IDC_COL_HIDE, OnHide)
        COMMAND_ID_HANDLER(IDC_COL_DEFAULT, OnDefault)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_COL_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Column", LVCFMT_LEFT, 138);
        Populate();
        return TRUE;
    }

    void Populate()
    {
        m_list.DeleteAllItems();
        std::vector<bool> shown(m_count, false);
        int row = 0;
        for (int c : m_state.visible)
        {
            if (c < 0 || c >= m_count) continue;
            m_list.InsertItem(row, m_defs[c].title);
            m_list.SetItemData(row, c);
            m_list.SetCheckState(row, TRUE);
            shown[c] = true;
            ++row;
        }
        for (int c = 0; c < m_count; ++c)
            if (!shown[c])
            {
                m_list.InsertItem(row, m_defs[c].title);
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
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&) { m_state.reset(m_defs, m_count); Populate(); return 0; }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        std::vector<int> vis, wid;
        for (int r = 0; r < m_list.GetItemCount(); ++r)
            if (m_list.GetCheckState(r))
            {
                int c = (int)m_list.GetItemData(r);
                vis.push_back(c);
                wid.push_back(m_defs[c].width);
            }
        if (!vis.empty()) { m_state.visible = vis; m_state.widths = wid; }
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ---- Capture Options -----------------------------------------------------
class CCaptureDlg : public CDialogImpl<CCaptureDlg>
{
public:
    enum { IDD = IDD_CAPTURE };

    Config& m_cfg;
    CListViewCtrl m_adapters;
    std::vector<Adapter> m_list;

    explicit CCaptureDlg(Config& cfg) : m_cfg(cfg) {}

    BEGIN_MSG_MAP(CCaptureDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Capture Options");
        int radio = IDC_CAP_RAW + (m_cfg.captureMethod >= 0 && m_cfg.captureMethod <= 3 ? m_cfg.captureMethod : 0);
        CheckRadioButton(IDC_CAP_RAW, IDC_CAP_NETMON3, radio);
        CheckDlgButton(IDC_CAP_PROMISC, m_cfg.promiscuousMode ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_CAP_FIREWALL, m_cfg.addToWindowsFirewall ? BST_CHECKED : BST_UNCHECKED);

        m_adapters.Attach(GetDlgItem(IDC_CAP_ADAPTERS));
        m_adapters.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_adapters.InsertColumn(0, L"Description", LVCFMT_LEFT, 180);
        m_adapters.InsertColumn(1, L"Adapter Name", LVCFMT_LEFT, 140);

        m_list = EnumerateAdapters();
        for (size_t i = 0; i < m_list.size(); ++i)
        {
            int row = m_adapters.InsertItem((int)i, m_list[i].description.c_str());
            m_adapters.SetItemText(row, 1, m_list[i].name.c_str());
            if (!m_cfg.adapterName.empty() && m_list[i].name == m_cfg.adapterName)
                m_adapters.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        return TRUE;
    }

    int selectedMethod()
    {
        if (IsDlgButtonChecked(IDC_CAP_WINPCAP)) return CAP_WINPCAP;
        if (IsDlgButtonChecked(IDC_CAP_NETMON))  return CAP_NETMON;
        if (IsDlgButtonChecked(IDC_CAP_NETMON3)) return CAP_NETMON3;
        return CAP_RAW;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        m_cfg.captureMethod = selectedMethod();
        m_cfg.promiscuousMode = IsDlgButtonChecked(IDC_CAP_PROMISC) != 0;
        m_cfg.addToWindowsFirewall = IsDlgButtonChecked(IDC_CAP_FIREWALL) != 0;
        int sel = m_adapters.GetNextItem(-1, LVNI_SELECTED);
        if (sel >= 0 && sel < (int)m_list.size()) m_cfg.adapterName = m_list[sel].name;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};
