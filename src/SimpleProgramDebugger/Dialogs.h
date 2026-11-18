// Dialogs.h -- About, Properties, Start New Process, Select Process and Column
// Settings dialogs, reusing the original's dialog templates (112, 105, 114, 113,
// 1096).
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include "resource.h"
#include "DebugEvent.h"
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
        SetWindowTextW(L"About SimpleProgramDebugger");
        SetDlgItemTextW(1002, L"SimpleProgramDebugger v1.16");
        SetDlgItemTextW(1004, L"Independent source-accurate recreation of SimpleProgramDebugger by NirSoft, through reverse engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        if (CWindow t1 = GetDlgItem(1005)) t1.ShowWindow(SW_HIDE);
        if (CWindow t2 = GetDlgItem(1006)) t2.ShowWindow(SW_HIDE);
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Properties (Property / Value list for one record) -------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>, public CDialogResize<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    const DbgRow& m_rec;
    const std::vector<int>& m_cols;
    CListViewCtrl m_list;

    CPropertiesDlg(const DbgRow& r, const std::vector<int>& cols) : m_rec(r), m_cols(cols) {}

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
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 380);
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

// ---- Start New Process ---------------------------------------------------
class CStartProcessDlg : public CDialogImpl<CStartProcessDlg>
{
public:
    enum { IDD = IDD_STARTPROC };

    std::wstring m_path, m_params, m_folder;

    CStartProcessDlg(std::wstring path, std::wstring params, std::wstring folder)
        : m_path(std::move(path)), m_params(std::move(params)), m_folder(std::move(folder)) {}

    BEGIN_MSG_MAP(CStartProcessDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDC_START_BROWSE, OnBrowse)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Start New Process");
        SetDlgItemTextW(IDC_START_PROCESS, m_path.c_str());
        SetDlgItemTextW(IDC_START_PARAMS, m_params.c_str());
        SetDlgItemTextW(IDC_START_FOLDER, m_folder.c_str());
        return TRUE;
    }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        CFileDialog dlg(TRUE, L"exe", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
            L"Executable File (*.exe)\0*.exe\0All Files (*.*)\0*.*\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Choose .exe file to debug";
        if (dlg.DoModal(m_hWnd) == IDOK)
            SetDlgItemTextW(IDC_START_PROCESS, dlg.m_szFileName);
        return 0;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        wchar_t buf[2048]{};
        GetDlgItemTextW(IDC_START_PROCESS, buf, 2048); m_path = buf;
        GetDlgItemTextW(IDC_START_PARAMS, buf, 2048);  m_params = buf;
        GetDlgItemTextW(IDC_START_FOLDER, buf, 2048);  m_folder = buf;
        if (m_path.empty()) return 0;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ---- Select Process (attach) ---------------------------------------------
class CSelectProcessDlg : public CDialogImpl<CSelectProcessDlg>, public CDialogResize<CSelectProcessDlg>
{
public:
    enum { IDD = IDD_SELECTPROC };

    DWORD m_pid = 0;
    CListViewCtrl m_list;

    BEGIN_DLGRESIZE_MAP(CSelectProcessDlg)
        DLGRESIZE_CONTROL(IDC_SELECT_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
        DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_X | DLSZ_MOVE_Y)
        DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
    END_DLGRESIZE_MAP()

    BEGIN_MSG_MAP(CSelectProcessDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        NOTIFY_HANDLER(IDC_SELECT_LIST, NM_DBLCLK, OnDblClk)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        CHAIN_MSG_MAP(CDialogResize<CSelectProcessDlg>)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Select a process");
        m_list.Attach(GetDlgItem(IDC_SELECT_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Process ID", LVCFMT_LEFT, 70);
        m_list.InsertColumn(1, L"Process Name", LVCFMT_LEFT, 150);
        m_list.InsertColumn(2, L"Full Path", LVCFMT_LEFT, 170);
        Populate();
        DlgResize_Init(true, true);
        return TRUE;
    }

    void Populate()
    {
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe{ sizeof(pe) };
        int row = 0;
        for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe))
        {
            int i = m_list.InsertItem(row++, std::to_wstring(pe.th32ProcessID).c_str());
            m_list.SetItemData(i, pe.th32ProcessID);
            m_list.SetItemText(i, 1, pe.szExeFile);
            std::wstring full;
            if (HANDLE hp = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID))
            {
                wchar_t path[1024]; DWORD n = 1024;
                if (::QueryFullProcessImageNameW(hp, 0, path, &n)) full.assign(path, n);
                ::CloseHandle(hp);
            }
            m_list.SetItemText(i, 2, full.c_str());
        }
        ::CloseHandle(snap);
    }

    DWORD selectedPid()
    {
        int i = m_list.GetNextItem(-1, LVNI_SELECTED);
        return i >= 0 ? (DWORD)m_list.GetItemData(i) : 0;
    }

    LRESULT OnDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnOK(0, 0, nullptr, b); return 0; }
    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        m_pid = selectedPid();
        if (m_pid == 0) return 0;
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
