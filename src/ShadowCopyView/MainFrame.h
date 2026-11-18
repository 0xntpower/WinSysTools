// MainFrame.h -- WTL main window for the ShadowCopyView recreation. The original
// registers a plain window class and hangs two SysListView32 controls, a toolbar, a
// status bar and an editable path band directly off it, with a draggable horizontal
// splitter between the snapshot list (top) and the file list (bottom). This rebuilds
// that layout cleanly over the EnumerateSnapshots and EnumerateFiles model.
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
#include "Records.h"
#include "Columns.h"
#include "Config.h"
#include "Export.h"
#include "Dialogs.h"
#include "VssEnumerator.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"ShadowCopyView", IDR_MAIN_MENU)

    CListViewCtrl m_snapList;
    CListViewCtrl m_fileList;
    CEdit m_filePath;

    Config m_cfg;
    bool m_noSaveLoad = false;

    std::vector<SnapshotRecord> m_snaps;
    std::vector<FileRecord> m_files;
    std::wstring m_curFolder; // folder inside the selected snapshot, empty is root
    int m_selSnap = -1;

    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    int m_snapPaneH = 0; // snapshot pane height, drives the splitter
    bool m_dragging = false;
    static const int kSplitBar = 6;
    static const int kPathH = 23;

    DisplayOptions disp() const
    {
        DisplayOptions d;
        d.showTimeInGmt = m_cfg.showTimeInGmt;
        return d;
    }
    bool focusIsFile() const
    {
        return ::GetFocus() == m_fileList.m_hWnd || ::GetFocus() == m_filePath.m_hWnd;
    }

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
    MSG_WM_SETCURSOR(OnSetCursor)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClk)
    NOTIFY_CODE_HANDLER(NM_RCLICK, OnRClick)
    NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
    COMMAND_ID_HANDLER(ID_SCV_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_SCV_OPEN_EXPLORER, OnOpenExplorer)
    COMMAND_ID_HANDLER(ID_SCV_LEVELUP, OnLevelUp)
    COMMAND_ID_HANDLER(ID_SCV_GOTOROOT, OnGotoRoot)
    COMMAND_ID_HANDLER(ID_SCV_COPYFILESTO, OnCopyFilesTo)
    COMMAND_ID_HANDLER(ID_SCV_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_SCV_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_SCV_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_SCV_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_SCV_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_SCV_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_SCV_CHOOSECOLS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_SCV_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_SCV_AUTOSIZE_HDR, OnAutoSizeHdr)
    COMMAND_ID_HANDLER(ID_SCV_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_SCV_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_SCV_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_SCV_TOOLTIPS, OnTooltips)
    COMMAND_ID_HANDLER(ID_SCV_MARKODDEVEN, OnMarkOddEven)
    COMMAND_ID_HANDLER(ID_SCV_SHOWTIMEGMT, OnShowTimeGmt)
    COMMAND_ID_HANDLER(ID_SCV_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_SCV_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()
    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();

    // ---- layout ---------------------------------------------------------
    int toolbarHeight();
    int statusHeight();
    void OnSize(UINT type, CSize);
    CRect splitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hitTest, UINT message);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);

    // ---- columns --------------------------------------------------------
    void RebuildColumns(CListViewCtrl& list, const ColumnState& st, const ColumnDef* defs);
    void ApplyGridLines();

    // ---- data -----------------------------------------------------------
    void Refresh();
    void LoadFiles();

    template <class Rec> void SortList(std::vector<Rec>& items, const ColumnState& st)
    {
        int col = st.sortColumn;
        bool asc = st.sortAscending;
        std::stable_sort(items.begin(), items.end(),
                         [&](const Rec& a, const Rec& b)
                         {
                             bool na = false, nb = false;
                             long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
                             int c;
                             if (na && nb)
                             {
                                 c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                             }
                             else
                             {
                                 c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
                             }
                             return asc ? (c < 0) : (c > 0);
                         });
    }

    // ---- navigation -----------------------------------------------------
    void NavigateInto(const std::wstring& folderName);
    void NavigateFromPathEdit();

    // ---- owner-data -----------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR p, BOOL&);
    LRESULT OnDblClk(int, LPNMHDR p, BOOL&);
    LRESULT OnListKeyDown(int, LPNMHDR p, BOOL&);
    LRESULT OnRClick(int, LPNMHDR p, BOOL&);

    // ---- status ---------------------------------------------------------
    void UpdateStatus();

    // ---- selection helpers ---------------------------------------------
    std::vector<int> selectedRows(CListViewCtrl& list);
    ExportRequest buildRequest(bool snap);

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnOpenExplorer(WORD, WORD, HWND, BOOL&);
    LRESULT OnLevelUp(WORD, WORD, HWND, BOOL&);
    LRESULT OnGotoRoot(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyFilesTo(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    void ShowHtml(bool snap, bool selectedOnly);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    void AutoSize(int how);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&);
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&);
    void SyncChecks();

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    void OnFindReplaceMsg(LPARAM lParam);
    void FindText(const std::wstring& what, bool down);
    void CopyToClipboard(const std::wstring& text);
    void OnDestroy();
};
