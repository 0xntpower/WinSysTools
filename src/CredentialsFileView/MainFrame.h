// MainFrame.h -- WTL main window for the CredentialsFileView recreation.
// Layout (menu 102, toolbar bitmap 110, status bar, a horizontal splitter whose
// upper pane is an owner-data report ListView and whose lower pane shows the
// selected entry's decrypted bytes as a hex dump or a strings list) matches the
// original. Logic is clean C++20 over the CredFile decode model.
//
// The method bodies live in MainFrame.cpp. This header keeps the class shape:
// members, the WTL macros, the message map and the method declarations.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlsplit.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "CredRecord.h"
#include "CredFile.h"
#include "Columns.h"
#include "Export.h"
#include "Config.h"
#include "Dialogs.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"CredentialsFileView", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    CEdit m_lower;
    HFONT m_lowerFont = nullptr;

    // Manual horizontal splitter: m_splitPos is the upper (list) pane height in
    // pixels within the area between the toolbar and the status bar. The original
    // positions its two panes by hand the same way (DeferWindowPos).
    static constexpr int kBar = 4;
    int m_splitPos = 0;
    bool m_dragging = false;

    std::vector<CredRecord> m_all;
    std::vector<CredRecord> m_items;
    Config m_cfg;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    int m_rclickRow = -1, m_rclickCol = -1;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);
    bool m_promptOnShow = true; // show the Decryption Options dialog on first display

    // ---- message filter -------------------------------------------------
    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    if (uMsg == m_uFindMsg)
    {
        OnFindReplaceMsg(lParam);
        lResult = 0;
        return TRUE;
    }
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_SETCURSOR(OnSetCursor)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ODSTATECHANGED, OnStateChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
    COMMAND_ID_HANDLER(ID_CFV_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_CFV_SAVE, OnSaveSelected)
    COMMAND_ID_HANDLER(ID_CFV_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_CFV_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_CFV_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_CFV_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_CFV_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_CFV_TOOLTIPS, OnTooltips)
    COMMAND_ID_HANDLER(ID_CFV_MARKODDEVEN, OnMarkOddEven)
    COMMAND_ID_HANDLER(ID_CFV_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_CFV_AUTOSIZE_HDR, OnAutoSizeHdr)
    COMMAND_ID_HANDLER(ID_CFV_CHOOSECOLS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_CFV_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_CFV_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_CFV_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_CFV_DECRYPT_OPTIONS, OnDecryptOptions)
    COMMAND_ID_HANDLER(ID_CFV_SHOWTIMEGMT, OnShowTimeGmt)
    COMMAND_ID_HANDLER(ID_CFV_RUNASADMIN, OnRunAsAdmin)
    COMMAND_ID_HANDLER(ID_CFV_LOWERPANE_HEX, OnLowerPaneMode)
    COMMAND_ID_HANDLER(ID_CFV_LOWERPANE_STR, OnLowerPaneMode)
    COMMAND_ID_HANDLER(ID_CFV_ENC_DEFAULT, OnEncoding)
    COMMAND_ID_HANDLER(ID_CFV_ENC_ANSI, OnEncoding)
    COMMAND_ID_HANDLER(ID_CFV_ENC_UTF16, OnEncoding)
    COMMAND_ID_HANDLER(ID_CFV_ENC_UTF8, OnEncoding)
    COMMAND_ID_HANDLER(ID_CFV_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_CFV_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    // Show the Decryption Options dialog once the window is up, unless a headless
    // path already filled the list.
    void PromptAndDecrypt();

    // ---- two-pane layout (manual splitter) ------------------------------
    int clientTopOffset() const;
    int clientBottomOffset() const;
    void PositionPanes();
    void OnSize(UINT, CSize);
    bool overBar(int y) const;
    void OnLButtonDown(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);
    void OnMouseMove(UINT, CPoint pt);
    BOOL OnSetCursor(CWindow wnd, UINT hitTest, UINT);
    void CreateToolbar();

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void ApplyGridLines();
    void SyncMenuChecks();
    DecodeRequest request() const;

    // ---- data -----------------------------------------------------------
    void Refresh();
    void ApplyFilterAndSort();
    void SortItems();
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
    LRESULT OnStateChanged(int, LPNMHDR, BOOL&);
    void UpdateStatus();

    // ---- lower pane -----------------------------------------------------
    void UpdateLowerPane();
    static std::wstring BuildHexDump(const std::vector<BYTE>& d);
    static std::wstring BuildStrings(const std::vector<BYTE>& d);

    // ---- selection helpers ---------------------------------------------
    std::vector<CredRecord> SelectedItems();
    ExportOptions expOpt();

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnDecryptOptions(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&);
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&);
    LRESULT OnEncoding(WORD, WORD id, HWND, BOOL&);
    LRESULT OnLowerPaneMode(WORD, WORD id, HWND, BOOL&);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&);
    void SyncWidths();
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnSaveSelected(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    void ShowHtml(const std::vector<CredRecord>& items);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnRunAsAdmin(WORD, WORD, HWND, BOOL&);

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
