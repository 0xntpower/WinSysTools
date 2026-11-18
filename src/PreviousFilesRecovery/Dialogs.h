// Dialogs.h -- About, Properties, Column Settings and Recover Files dialogs, reusing
// the original's dialog templates (112, 105, 1096, 113).
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <string>
#include <vector>
#include "resource.h"
#include "Columns.h"
#include "PrevFileRecord.h"

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
        SetWindowTextW(L"About PreviousFilesRecovery");
        SetDlgItemTextW(IDC_ABOUT_TITLE, L"PreviousFilesRecovery v1.10");
        SetDlgItemTextW(IDC_ABOUT_COPYRIGHT, L"Independent source-accurate recreation of PreviousFilesRecovery by NirSoft, through reverse engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
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

    const PrevFileRecord& m_rec;
    const std::vector<int>& m_cols;
    DisplayOptions m_disp;
    CListViewCtrl m_list;

    CPropertiesDlg(const PrevFileRecord& r, const std::vector<int>& cols, DisplayOptions d)
        : m_rec(r), m_cols(cols), m_disp(d) {}

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
        m_list.InsertColumn(0, L"Property", LVCFMT_LEFT, 160);
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 360);
        for (int c : m_cols)
        {
            int row = m_list.InsertItem(m_list.GetItemCount(), kColumns[c].title);
            m_list.SetItemText(row, 1, m_rec.column(c, m_disp).c_str());
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
        NOTIFY_HANDLER(IDC_COL_LIST, LVN_ITEMCHANGED, OnSelChange)
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
        for (size_t k = 0; k < m_visible.size(); ++k)
        {
            int c = m_visible[k];
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
    int widthFor(int col)
    {
        for (size_t i = 0; i < m_visible.size(); ++i)
            if (m_visible[i] == col) return i < m_widths.size() ? m_widths[i] : kColumns[col].width;
        return kColumns[col].width;
    }

    LRESULT OnSelChange(int, LPNMHDR, BOOL&)
    {
        int i = sel();
        if (i >= 0) SetDlgItemInt(IDC_COL_WIDTH, widthFor((int)m_list.GetItemData(i)), FALSE);
        return 0;
    }

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
        // Apply an edited width to the selected column before reading the order back.
        std::vector<int> vis, wid;
        int editW = (int)GetDlgItemInt(IDC_COL_WIDTH, nullptr, FALSE);
        int selCol = sel() >= 0 ? (int)m_list.GetItemData(sel()) : -1;
        for (int r = 0; r < m_list.GetItemCount(); ++r)
            if (m_list.GetCheckState(r))
            {
                int c = (int)m_list.GetItemData(r);
                vis.push_back(c);
                wid.push_back((c == selCol && editW > 0) ? editW : widthFor(c));
            }
        if (!vis.empty()) { m_visible = vis; m_widths = wid; }
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ---- Recover Files -------------------------------------------------------
// Copies each selected item's previous (shadow-copy) version into a chosen folder.
// When a name already exists in the destination a new name is used, as the readme says.
class CRecoverDlg : public CDialogImpl<CRecoverDlg>
{
public:
    enum { IDD = IDD_RECOVER };

    std::vector<PrevFileRecord> m_items;
    explicit CRecoverDlg(std::vector<PrevFileRecord> items) : m_items(std::move(items)) {}

    BEGIN_MSG_MAP(CRecoverDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDC_REC_DOIT, OnDoIt)
        COMMAND_ID_HANDLER(IDC_REC_BROWSE, OnBrowse)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
        COMMAND_ID_HANDLER(2, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Recover Files");
        return TRUE;
    }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        CFolderDialog dlg(m_hWnd, L"Choose destination folder:");
        if (dlg.DoModal(m_hWnd) == IDOK) SetDlgItemTextW(IDC_REC_DEST, dlg.GetFolderPath());
        return 0;
    }

    static bool copyRecursive(const std::wstring& src, const std::wstring& dst)
    {
        DWORD a = ::GetFileAttributesW(src.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return false;
        if (!(a & FILE_ATTRIBUTE_DIRECTORY))
            return ::CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0;
        ::CreateDirectoryW(dst.c_str(), nullptr);
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW((src + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return true;
        do
        {
            if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 ||
                (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
            copyRecursive(src + L"\\" + fd.cFileName, dst + L"\\" + fd.cFileName);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
        return true;
    }

    static std::wstring uniqueName(const std::wstring& dir, const std::wstring& name)
    {
        std::wstring full = dir + L"\\" + name;
        if (::GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) return full;
        std::wstring base = name, ext;
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot != 0) { base = name.substr(0, dot); ext = name.substr(dot); }
        for (int i = 2; i < 10000; ++i)
        {
            std::wstring cand = dir + L"\\" + base + L" (" + std::to_wstring(i) + L")" + ext;
            if (::GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
        }
        return full;
    }

    LRESULT OnDoIt(WORD, WORD, HWND, BOOL&)
    {
        wchar_t dest[MAX_PATH]{};
        GetDlgItemTextW(IDC_REC_DEST, dest, MAX_PATH);
        if (dest[0] == 0 || ::GetFileAttributesW(dest) == INVALID_FILE_ATTRIBUTES)
        {
            MessageBoxW(L"You must provide a valid destination folder to copy the files !",
                        L"PreviousFilesRecovery", MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }
        int done = 0;
        for (const auto& r : m_items)
        {
            std::wstring out = uniqueName(dest, r.filename);
            SetDlgItemTextW(IDC_REC_LINE1, (L"Copying: " + r.filename).c_str());
            if (copyRecursive(r.previousPath(), out)) ++done;
        }
        MessageBoxW(L"Finished the copy process !", L"PreviousFilesRecovery", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    LRESULT OnClose(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};
