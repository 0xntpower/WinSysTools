// Dialogs.h -- About, Properties, Column Settings and Copy Files dialogs, reusing the
// original's dialog templates (112, 105, 1096, 113). The Properties and Column
// Settings dialogs are generic over a column table so both panes share them.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <utility>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "Records.h"
#include "VssEnumerator.h"

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
        SetWindowTextW(L"About ShadowCopyView");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"ShadowCopyView v1.16");
        SetDlgItemTextW(IDC_ABOUT_DESC,
                        L"Independent source-accurate recreation of ShadowCopyView by NirSoft, through reverse "
                        L"engineering and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        if (CWindow t1 = GetDlgItem(1005))
        {
            t1.ShowWindow(SW_HIDE);
        }
        if (CWindow t2 = GetDlgItem(1006))
        {
            t2.ShowWindow(SW_HIDE);
        }
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Properties (Property / Value list for one record) -------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>, public CDialogResize<CPropertiesDlg>
{
  public:
    enum
    {
        IDD = IDD_PROPERTIES
    };

    std::vector<std::pair<std::wstring, std::wstring>> m_rows;
    CListViewCtrl m_list;

    explicit CPropertiesDlg(std::vector<std::pair<std::wstring, std::wstring>> rows)
        : m_rows(std::move(rows))
    {
    }

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
        if (CWindow lbl = GetDlgItem(IDC_PROP_LABEL))
        {
            lbl.ShowWindow(SW_HIDE);
        }
        if (CWindow edit = GetDlgItem(IDC_PROP_VALUE))
        {
            edit.ShowWindow(SW_HIDE);
        }

        CRect rc;
        GetClientRect(&rc);
        rc.bottom -= 28;
        m_list.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE,
                      IDC_PROP_VALUE + 100);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Property", LVCFMT_LEFT, 150);
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 380);
        for (const auto& [name, value] : m_rows)
        {
            int row = m_list.InsertItem(m_list.GetItemCount(), name.c_str());
            m_list.SetItemText(row, 1, value.c_str());
        }
        DlgResize_Init(true, true);
        return TRUE;
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

    const ColumnDef* m_defs;
    int m_count;
    ColumnState m_state;
    CListViewCtrl m_list;

    CColumnsDlg(const ColumnDef* defs, int count, ColumnState state)
        : m_defs(defs)
        , m_count(count)
        , m_state(std::move(state))
    {
    }

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
            if (c < 0 || c >= m_count)
            {
                continue;
            }
            m_list.InsertItem(row, m_defs[c].title);
            m_list.SetItemData(row, c);
            m_list.SetCheckState(row, TRUE);
            shown[c] = true;
            ++row;
        }
        for (int c = 0; c < m_count; ++c)
        {
            if (!shown[c])
            {
                m_list.InsertItem(row, m_defs[c].title);
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
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_state.reset(m_defs, m_count);
        Populate();
        return 0;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        std::vector<int> vis, wid;
        for (int r = 0; r < m_list.GetItemCount(); ++r)
        {
            if (m_list.GetCheckState(r))
            {
                int c = (int)m_list.GetItemData(r);
                vis.push_back(c);
                wid.push_back(m_defs[c].width);
            }
        }
        if (!vis.empty())
        {
            m_state.visible = vis;
            m_state.widths = wid;
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

// ---- Copy Files ----------------------------------------------------------
class CCopyFilesDlg : public CDialogImpl<CCopyFilesDlg>
{
  public:
    enum
    {
        IDD = IDD_COPYFILES
    };

    const SnapshotRecord& m_snap;
    std::wstring m_folder;
    std::vector<std::wstring> m_names;

    CCopyFilesDlg(const SnapshotRecord& snap, std::wstring folder, std::vector<std::wstring> names)
        : m_snap(snap)
        , m_folder(std::move(folder))
        , m_names(std::move(names))
    {
    }

    BEGIN_MSG_MAP(CCopyFilesDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_COPY_BROWSE, OnBrowse)
    COMMAND_ID_HANDLER(IDC_COPY_DOIT, OnDoIt)
    COMMAND_ID_HANDLER(IDC_COPY_CLOSE, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Copy Files");
        return TRUE;
    }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        BROWSEINFOW bi{};
        bi.hwndOwner = m_hWnd;
        bi.lpszTitle = L"Choose destination folder:";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
        if (pidl)
        {
            wchar_t path[MAX_PATH]{};
            if (::SHGetPathFromIDListW(pidl, path))
            {
                SetDlgItemTextW(IDC_COPY_DEST, path);
            }
            ::CoTaskMemFree(pidl);
        }
        return 0;
    }

    LRESULT OnDoIt(WORD, WORD, HWND, BOOL&)
    {
        wchar_t dest[MAX_PATH]{};
        GetDlgItemTextW(IDC_COPY_DEST, dest, MAX_PATH);
        if (dest[0] == 0 || ::GetFileAttributesW(dest) == INVALID_FILE_ATTRIBUTES)
        {
            MessageBoxW(L"You must provide a valid destination folder to copy the files !", L"ShadowCopyView",
                        MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }
        int n = CopyItemsFromSnapshot(m_snap, m_folder, m_names, dest);
        wchar_t msg[64];
        swprintf(msg, 64, L"Finished the copy process ! (%d file(s))", n);
        SetDlgItemTextW(IDC_COPY_STATUS1, msg);
        MessageBoxW(L"Finished the copy process !", L"ShadowCopyView", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LRESULT OnClose(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};
