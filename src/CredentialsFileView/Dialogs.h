// Dialogs.h -- About, Properties, Credentials Decryption Options and Column
// Settings dialogs, reusing the original's templates (112, 105, 113, 1096).
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
#include "CredRecord.h"
#include "Columns.h"
#include "Config.h"

inline std::wstring ExpandEnv(const std::wstring& s)
{
    DWORD n = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (!n) return s;
    std::wstring out(n, L'\0');
    ::ExpandEnvironmentStringsW(s.c_str(), out.data(), n);
    out.resize(n ? n - 1 : 0);
    return out;
}

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
        SetWindowTextW(L"About CredentialsFileView");
        SetDlgItemTextW(IDC_ABOUT_PRODUCT, L"CredentialsFileView v1.12");
        SetDlgItemTextW(IDC_ABOUT_COPYRIGHT, L"Copyright (c) 2016 - 2023 Nir Sofer");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Properties (Property / Value list for one record) -------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>, public CDialogResize<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    const CredRecord& m_rec;
    const std::vector<int>& m_cols;
    CListViewCtrl m_list;

    CPropertiesDlg(const CredRecord& r, const std::vector<int>& cols) : m_rec(r), m_cols(cols) {}

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

// ---- Credentials Decryption Options (startup dialog) ---------------------
class CDecryptOptionsDlg : public CDialogImpl<CDecryptOptionsDlg>
{
public:
    enum { IDD = IDD_DECRYPT_OPTIONS };

    Config& m_cfg;
    explicit CDecryptOptionsDlg(Config& cfg) : m_cfg(cfg) {}

    BEGIN_MSG_MAP(CDecryptOptionsDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    static std::wstring textOf(CWindow w)
    {
        int n = w.GetWindowTextLengthW();
        std::wstring s(n + 1, L'\0');
        w.GetWindowTextW(s.data(), n + 1);
        s.resize(n);
        return s;
    }

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Credentials Decryption Options");

        CComboBox source = GetDlgItem(IDC_DEC_SOURCE);
        for (UINT id = IDS_SOURCE_BASE + 1; id <= IDS_SOURCE_BASE + 4; ++id)
        {
            wchar_t buf[256]{};
            ::LoadStringW(ModuleHelper::GetResourceInstance(), id, buf, 256);
            source.AddString(buf);
        }
        int sel = (m_cfg.dataSource >= 1 && m_cfg.dataSource <= 4) ? m_cfg.dataSource - 1 : 1;
        source.SetCurSel(sel);

        // Auto-fill the current user's folders when nothing is stored yet.
        std::wstring cred1 = m_cfg.credFolder1.empty() ? ExpandEnv(L"%LOCALAPPDATA%\\Microsoft\\Credentials") : m_cfg.credFolder1;
        std::wstring cred2 = m_cfg.credFolder2.empty() ? ExpandEnv(L"%APPDATA%\\Microsoft\\Credentials") : m_cfg.credFolder2;
        std::wstring prot1 = m_cfg.protectFolder1.empty() ? ExpandEnv(L"%APPDATA%\\Microsoft\\Protect") : m_cfg.protectFolder1;
        std::wstring prot2 = m_cfg.protectFolder2.empty() ? ExpandEnv(L"%WINDIR%\\System32\\Microsoft\\Protect") : m_cfg.protectFolder2;
        std::wstring hive = m_cfg.regHiveFolder.empty() ? ExpandEnv(L"%WINDIR%\\System32\\Config") : m_cfg.regHiveFolder;

        SetDlgItemTextW(IDC_DEC_CREDFOLDER1, cred1.c_str());
        SetDlgItemTextW(IDC_DEC_CREDFOLDER2, cred2.c_str());
        SetDlgItemTextW(IDC_DEC_CREDFOLDER3, m_cfg.credFolder3.c_str());
        SetDlgItemTextW(IDC_DEC_PROTECT1, prot1.c_str());
        SetDlgItemTextW(IDC_DEC_PROTECT2, prot2.c_str());
        SetDlgItemTextW(IDC_DEC_REGHIVE, hive.c_str());
        SetDlgItemTextW(IDC_DEC_PASSWORD, m_cfg.password_.c_str());

        if (CComboBox user = GetDlgItem(IDC_DEC_USERNAME); user.m_hWnd)
        {
            wchar_t name[256]{}; DWORD cch = 256;
            if (::GetUserNameW(name, &cch)) user.AddString(name);
            user.SetWindowTextW(m_cfg.userName.empty() ? name : m_cfg.userName.c_str());
        }
        return TRUE;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        CComboBox source = GetDlgItem(IDC_DEC_SOURCE);
        int sel = source.GetCurSel();
        m_cfg.dataSource = (sel >= 0 && sel <= 3) ? sel + 1 : 2;
        m_cfg.credFolder1 = textOf(GetDlgItem(IDC_DEC_CREDFOLDER1));
        m_cfg.credFolder2 = textOf(GetDlgItem(IDC_DEC_CREDFOLDER2));
        m_cfg.credFolder3 = textOf(GetDlgItem(IDC_DEC_CREDFOLDER3));
        m_cfg.protectFolder1 = textOf(GetDlgItem(IDC_DEC_PROTECT1));
        m_cfg.protectFolder2 = textOf(GetDlgItem(IDC_DEC_PROTECT2));
        m_cfg.regHiveFolder = textOf(GetDlgItem(IDC_DEC_REGHIVE));
        m_cfg.password_ = textOf(GetDlgItem(IDC_DEC_PASSWORD));
        if (CWindow user = GetDlgItem(IDC_DEC_USERNAME); user.m_hWnd) m_cfg.userName = textOf(user);
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
