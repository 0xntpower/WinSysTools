// MainFrame.h -- WTL main window for the SimpleWMIView recreation.
//
// Visual layout matches the original: a toolbar (bitmap 110), the WMI options
// bar (child dialog 111), an owner-data report ListView whose columns are the
// WMI result properties, and a status bar. Logic is clean C++20 over the
// RunWmiQuery() model rather than the decompiled offset code.
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
#include <atldlgs.h>
#include <atlmisc.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "WmiResult.h"
#include "WmiQuery.h"
#include "Export.h"
#include "Config.h"
#include "Dialogs.h"
#include "OptionsBar.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter, public IBarHost
{
  public:
    DECLARE_FRAME_WND_CLASS(L"SimpleWMIView", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    COptionsBar m_bar;
    WmiResult m_res;
    std::vector<int> m_cols;    // visible columns, display order
    std::vector<int> m_display; // row indices, display order
    Config m_cfg;
    std::wstring m_password;
    int m_barHeight = 160;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    int m_rclickRow = -1, m_rclickCol = -1;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);
    bool m_autoUpdate = false;

    DisplayOptions disp() const;

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
    MSG_WM_SIZE(OnSize)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SETFOCUS(OnSetFocus)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ODSTATECHANGED, OnStateChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_CUSTOMDRAW, OnCustomDraw)
    COMMAND_ID_HANDLER(ID_SWV_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_SWV_NEWINSTANCE, OnNewInstance)
    COMMAND_ID_HANDLER(ID_SWV_SAVE, OnSaveSelected)
    COMMAND_ID_HANDLER(ID_SWV_SAVEALL, OnSaveAll)
    COMMAND_ID_HANDLER(ID_SWV_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_SWV_COPYCELL, OnCopyCell)
    COMMAND_ID_HANDLER(ID_SWV_COPYSORTED, OnCopySorted)
    COMMAND_ID_HANDLER(ID_SWV_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_SWV_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_SWV_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_SWV_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_SWV_MARKODDEVEN, OnMarkOddEven)
    COMMAND_ID_HANDLER(ID_SWV_BLACKBG, OnBlackBg)
    COMMAND_ID_HANDLER(ID_SWV_ALIGNNUMERIC, OnAlignNumeric)
    COMMAND_ID_HANDLER(ID_SWV_SHOWGMT, OnShowGmt)
    COMMAND_ID_HANDLER(ID_SWV_SHOWMS, OnShowMs)
    COMMAND_ID_HANDLER(ID_SWV_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_SWV_AUTOSIZE_HDR, OnAutoSizeHdr)
    COMMAND_ID_HANDLER(ID_SWV_CHOOSECOLS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_SWV_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_SWV_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_SWV_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_SWV_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_SWV_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void OnSize(UINT type, CSize);
    void OnSetFocus(CWindow);

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void ApplyGridLines();
    void ApplyColors();
    void SyncMenuChecks();

    // ---- data -----------------------------------------------------------
    void OnBarUpdate() override;
    void DoRefresh();
    void BuildDisplay();
    void SortDisplay();
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnCustomDraw(int, LPNMHDR p, BOOL&);
    LRESULT OnStateChanged(int, LPNMHDR, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);

    // ---- status / title -------------------------------------------------
    void UpdateStatus();
    void UpdateTitle();

    // ---- selection helpers ---------------------------------------------
    std::vector<int> SelectedRows();
    WmiResult subset(const std::vector<int>& rowIdx);
    ExportOptions expOpt();

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnNewInstance(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnBlackBg(WORD, WORD, HWND, BOOL&);
    LRESULT OnAlignNumeric(WORD, WORD, HWND, BOOL&);
    LRESULT OnShowGmt(WORD, WORD, HWND, BOOL&);
    LRESULT OnShowMs(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyCell(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopySorted(WORD, WORD, HWND, BOOL&);
    LRESULT OnSaveSelected(WORD, WORD, HWND, BOOL&);
    LRESULT OnSaveAll(WORD, WORD, HWND, BOOL&);
    LRESULT DoSave(std::vector<int> rows);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    void ShowHtml(const std::vector<int>& rows);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
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
