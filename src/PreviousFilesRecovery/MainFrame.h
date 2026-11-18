// MainFrame.h -- WTL main window for the PreviousFilesRecovery recreation. The layout
// matches the original (menu 102, flat toolbar from bitmap 110, the embedded search
// panel dialog 111 docked under the toolbar, an owner-data report ListView for the
// results, and a status bar). Logic runs over the clean ScanPreviousFiles model
// instead of the decompiled offset code.
//
// The CMainFrame method bodies live in MainFrame.cpp. This header keeps the class
// shape: members, the WTL macros, the message map and the method declarations.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <uxtheme.h>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#pragma comment(lib, "uxtheme.lib")
#include "resource.h"
#include "../common/UiUtil.h"
#include "Columns.h"
#include "Config.h"
#include "PrevFileRecord.h"
#include "Scanner.h"
#include "ShadowCopy.h"
#include "Export.h"
#include "Dialogs.h"

extern CAppModule _Module;

// ---- embedded search-options panel (dialog 111) --------------------------
class CSearchPanel : public CDialogImpl<CSearchPanel>
{
  public:
    enum
    {
        IDD = IDD_SEARCH
    };

    std::function<void()> onStart;

    BEGIN_MSG_MAP(CSearchPanel)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_SRCH_BROWSE, OnBrowse)
    COMMAND_ID_HANDLER(IDC_SRCH_START, OnStart)
    COMMAND_HANDLER(IDC_SRCH_MODE, CBN_SELCHANGE, OnModeChange)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        CComboBox depth = GetDlgItem(IDC_SRCH_DEPTH);
        depth.AddString(L"Unlimited");
        for (int i = 1; i < 20; ++i)
        {
            depth.AddString(std::to_wstring(i).c_str());
        }
        depth.SetCurSel(0);

        CComboBox mode = GetDlgItem(IDC_SRCH_MODE);
        mode.AddString(L"Find all file copies");
        mode.AddString(L"Find all file copies with different size/write time");
        mode.AddString(L"Find only the latest file version");
        mode.AddString(L"Find only files in the selected shadow copy");
        mode.SetCurSel(1);

        FillShadows();
        return TRUE;
    }

    void FillShadows()
    {
        CComboBox shadow = GetDlgItem(IDC_SRCH_SHADOW);
        shadow.ResetContent();
        for (const auto& s : EnumerateShadowCopies())
        {
            shadow.AddString(s.deviceObject.c_str());
        }
        if (shadow.GetCount() > 0)
        {
            shadow.SetCurSel(0);
        }
        shadow.EnableWindow(GetMode() == 4);
    }

    void ApplyConfig(const Config& c)
    {
        SetDlgItemTextW(IDC_SRCH_BASEFOLDER, c.baseFolder.c_str());
        SetDlgItemTextW(IDC_SRCH_WILDCARD, c.filesWildcard.c_str());
        CComboBox(GetDlgItem(IDC_SRCH_DEPTH)).SetCurSel(std::clamp(c.subfoldersDepth, 0, 19));
        CComboBox(GetDlgItem(IDC_SRCH_MODE)).SetCurSel(std::clamp(c.multipleCopiesMode - 1, 0, 3));
        CheckDlgButton(IDC_SRCH_FINDPREV, c.findPreviousFiles ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SRCH_FINDDELFILE, c.findDeletedFiles ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SRCH_FINDDELDIR, c.findDeletedFolders ? BST_CHECKED : BST_UNCHECKED);
        CWindow(GetDlgItem(IDC_SRCH_SHADOW)).EnableWindow(GetMode() == 4);
    }

    void ReadConfig(Config& c) const
    {
        wchar_t buf[MAX_PATH]{};
        const_cast<CSearchPanel*>(this)->GetDlgItemTextW(IDC_SRCH_BASEFOLDER, buf, MAX_PATH);
        c.baseFolder = buf;
        wchar_t wc[128]{};
        const_cast<CSearchPanel*>(this)->GetDlgItemTextW(IDC_SRCH_WILDCARD, wc, 128);
        c.filesWildcard = wc[0] ? std::wstring(wc) : std::wstring(L"*");
        c.subfoldersDepth = CComboBox(const_cast<CSearchPanel*>(this)->GetDlgItem(IDC_SRCH_DEPTH)).GetCurSel();
        c.multipleCopiesMode = GetMode();
        c.findPreviousFiles = const_cast<CSearchPanel*>(this)->IsDlgButtonChecked(IDC_SRCH_FINDPREV) == BST_CHECKED;
        c.findDeletedFiles = const_cast<CSearchPanel*>(this)->IsDlgButtonChecked(IDC_SRCH_FINDDELFILE) == BST_CHECKED;
        c.findDeletedFolders = const_cast<CSearchPanel*>(this)->IsDlgButtonChecked(IDC_SRCH_FINDDELDIR) == BST_CHECKED;
    }

    int GetMode() const
    {
        return CComboBox(const_cast<CSearchPanel*>(this)->GetDlgItem(IDC_SRCH_MODE)).GetCurSel() + 1;
    }

    std::wstring SelectedShadow()
    {
        CComboBox shadow = GetDlgItem(IDC_SRCH_SHADOW);
        wchar_t buf[600]{};
        shadow.GetWindowTextW(buf, 600);
        return buf;
    }

    void SetStatus(const std::wstring& s)
    {
        SetDlgItemTextW(IDC_SRCH_STATUS, s.c_str());
    }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        CFolderDialog dlg(m_hWnd, L"Choose the base folder to scan");
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            SetDlgItemTextW(IDC_SRCH_BASEFOLDER, dlg.GetFolderPath());
        }
        return 0;
    }
    LRESULT OnStart(WORD, WORD, HWND, BOOL&)
    {
        if (onStart)
        {
            onStart();
        }
        return 0;
    }
    LRESULT OnModeChange(WORD, WORD, HWND, BOOL&)
    {
        CWindow(GetDlgItem(IDC_SRCH_SHADOW)).EnableWindow(GetMode() == 4);
        return 0;
    }
};

// ---- main window ---------------------------------------------------------
class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"PreviousFilesRecovery", IDR_MAIN_MENU)

    CSearchPanel m_panel;
    CListViewCtrl m_list;
    std::vector<PrevFileRecord> m_items;
    Config m_cfg;
    bool m_noSaveLoad = false;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    int m_rclickRow = -1, m_rclickCol = -1;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);
    int m_panelHeight = 0;

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    if (uMsg == m_uFindMsg)
    {
        OnFindReplaceMsg(lParam);
        lResult = 0;
        return TRUE;
    }
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ODSTATECHANGED, OnStateChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
    COMMAND_ID_HANDLER(ID_PFR_STARTSCAN, OnStartScan)
    COMMAND_ID_HANDLER(ID_PFR_REFRESH, OnStartScan)
    COMMAND_ID_HANDLER(ID_PFR_RECOVER, OnRecover)
    COMMAND_ID_HANDLER(ID_PFR_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_PFR_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_PFR_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_PFR_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_PFR_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_PFR_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_PFR_TOOLTIPS, OnTooltips)
    COMMAND_ID_HANDLER(ID_PFR_MARKODDEVEN, OnMarkOddEven)
    COMMAND_ID_HANDLER(ID_PFR_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_PFR_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_PFR_CHOOSECOLS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_PFR_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_PFR_AUTOSIZE_HDR, OnAutoSizeHdr)
    COMMAND_ID_HANDLER(ID_PFR_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_PFR_SHOWTIMEGMT, OnShowTimeGmt)
    COMMAND_ID_HANDLER(ID_PFR_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_PFR_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    LRESULT OnEraseBkgnd(CDCHandle dc);
    void OnSize(UINT type, CSize);

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void ApplyGridLines();
    void ApplyTooltips();
    void SyncMenuChecks();
    DisplayOptions disp() const;

    // ---- scan -----------------------------------------------------------
    void DoScan();
    void SortItems();
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnStateChanged(int, LPNMHDR, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
    void UpdateStatus();

    // ---- selection ------------------------------------------------------
    std::vector<PrevFileRecord> SelectedItems();
    ExportRequest makeRequest(const std::vector<PrevFileRecord>& items);

    // ---- commands -------------------------------------------------------
    LRESULT OnStartScan(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&);
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&);
    void SyncWidths();
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    void ShowHtml(const std::vector<PrevFileRecord>& items);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnRecover(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    void OnFindReplaceMsg(LPARAM lParam);
    void FindText(const std::wstring& what, bool down);

    // ---- notifications --------------------------------------------------
    LRESULT OnDblClk(int, LPNMHDR, BOOL&);
    LRESULT OnRClick(int, LPNMHDR p, BOOL&);
    void CopyToClipboard(const std::wstring& text);

    void OnDestroy();
};
