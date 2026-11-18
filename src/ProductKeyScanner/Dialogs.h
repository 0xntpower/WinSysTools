// Dialogs.h -- About, Scan Options, Column Settings and Properties dialogs. All four
// reuse the original extracted templates (112, 113, 1096, 105) for visual parity.
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
#include "ProductKey.h"
#include "Columns.h"
#include "Scanner.h"

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
        SetWindowTextW(L"About Product Key Scanner");
        SetDlgItemTextW(IDC_ABOUT_TEXT, L"Product Key Scanner v1.03");
        SetDlgItemTextW(IDC_ABOUT_LINK, L"http://www.nirsoft.net");
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
    LRESULT OnLink(WORD, WORD, HWND, BOOL&)
    {
        ::ShellExecuteW(m_hWnd, L"open", L"http://www.nirsoft.net", nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
};

// ----------------------------------------------------------- Scan Options (113)
class CScanOptionsDlg : public CDialogImpl<CScanOptionsDlg>
{
public:
    enum { IDD = IDD_SCANOPTIONS };

    ScanOptions m_opts;   // in / out

    explicit CScanOptionsDlg(const ScanOptions& opts) : m_opts(opts) {}

    BEGIN_MSG_MAP(CScanOptionsDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOk)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        COMMAND_ID_HANDLER(IDC_SCAN_SOURCE, OnSourceChange)
        COMMAND_ID_HANDLER(IDC_SCAN_BROWSE, OnBrowse)
        COMMAND_ID_HANDLER(IDC_SCAN_AUTOFILL, OnAutoFill)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Scan Options");
        SendDlgItemMessageW(IDC_SCAN_SOURCE, CB_ADDSTRING, 0, (LPARAM)L"Scan your current running system");
        SendDlgItemMessageW(IDC_SCAN_SOURCE, CB_ADDSTRING, 0, (LPARAM)L"Scan Registry file on external hard drive");
        SendDlgItemMessageW(IDC_SCAN_SOURCE, CB_SETCURSEL, (WPARAM)(int)m_opts.source, 0);
        CheckDlgButton(IDC_SCAN_WMI, m_opts.useWMI ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SCAN_BIOS, m_opts.useBIOS ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(IDC_SCAN_PATH, m_opts.hivePath.c_str());
        UpdateEnable();
        CenterWindow(GetParent());
        return TRUE;
    }

    void UpdateEnable()
    {
        bool ext = SendDlgItemMessageW(IDC_SCAN_SOURCE, CB_GETCURSEL, 0, 0) == 1;
        ::EnableWindow(GetDlgItem(IDC_SCAN_PATH), ext);
        ::EnableWindow(GetDlgItem(IDC_SCAN_BROWSE), ext);
        ::EnableWindow(GetDlgItem(IDC_SCAN_AUTOFILL), ext);
        ::EnableWindow(GetDlgItem(IDC_SCAN_WMI), !ext);
        ::EnableWindow(GetDlgItem(IDC_SCAN_BIOS), !ext);
    }

    LRESULT OnSourceChange(WORD code, WORD, HWND, BOOL& handled)
    {
        if (code == CBN_SELCHANGE) UpdateEnable();
        else handled = FALSE;
        return 0;
    }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
            L"Software Registry File (software)\0software\0All Files (*.*)\0*.*\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Choose the software Registry file to scan";
        if (dlg.DoModal() == IDOK)
            SetDlgItemTextW(IDC_SCAN_PATH, dlg.m_szFileName);
        return 0;
    }

    // Automatic Fill: suggest "<drive>:\Windows\System32\config\software" for the
    // first removable or fixed drive other than the system drive.
    LRESULT OnAutoFill(WORD, WORD, HWND, BOOL&)
    {
        wchar_t sysDir[MAX_PATH] = {};
        ::GetSystemDirectoryW(sysDir, MAX_PATH);
        wchar_t sysDrive = sysDir[0];
        DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (!(mask & (1u << i))) continue;
            wchar_t letter = (wchar_t)(L'A' + i);
            if (towupper(letter) == towupper(sysDrive)) continue;
            wchar_t rootPath[] = { letter, L':', L'\\', 0 };
            UINT type = ::GetDriveTypeW(rootPath);
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
            std::wstring p = std::wstring(1, letter) + L":\\Windows\\System32\\config\\software";
            SetDlgItemTextW(IDC_SCAN_PATH, p.c_str());
            return 0;
        }
        return 0;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        int src = (int)SendDlgItemMessageW(IDC_SCAN_SOURCE, CB_GETCURSEL, 0, 0);
        m_opts.source = (src == 1) ? ScanSource::ExternalHive : ScanSource::CurrentSystem;
        m_opts.useWMI = IsDlgButtonChecked(IDC_SCAN_WMI) == BST_CHECKED;
        m_opts.useBIOS = IsDlgButtonChecked(IDC_SCAN_BIOS) == BST_CHECKED;
        wchar_t path[MAX_PATH] = {};
        GetDlgItemTextW(IDC_SCAN_PATH, path, MAX_PATH);
        m_opts.hivePath = path;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ------------------------------------------------------- Column Settings (1096)
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
public:
    enum { IDD = IDD_COLUMNS };

    std::vector<int> m_visible;                 // in / out: visible columns in order
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
        COMMAND_ID_HANDLER(IDC_COL_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(IDC_COL_DESELECTALL, OnDeselectAll)
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
        CenterWindow(GetParent());
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
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { for (int i = 0; i < (int)m_order.size(); ++i) m_cols.SetCheckState(i, TRUE); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { for (int i = 0; i < (int)m_order.size(); ++i) m_cols.SetCheckState(i, FALSE); return 0; }
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
// Template 105 holds a label+edit row that the original clones per field.
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    const ProductKeyRecord& m_item;
    std::vector<int> m_cols;

    CPropertiesDlg(const ProductKeyRecord& item, const std::vector<int>& cols) : m_item(item), m_cols(cols) {}

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
