// MainFrame.h -- WTL main window for the HeapMemView recreation.
//
// Layout mirrors the original (sub_1400090AC): class "HeapMemView", menu 102,
// toolbar bitmap 104, a two-part status bar, an upper report ListView of heap
// blocks, and a lower read-only Courier New edit that shows the selected block as
// a hex dump. A draggable horizontal divider between the two panes stores its
// position in VerSplitLoc. Logic is clean C++20 over EnumerateHeapBlocks rather
// than the framework code.
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
#include <shellapi.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "HeapBlock.h"
#include "Columns.h"
#include "Config.h"
#include "Export.h"
#include "HexDump.h"
#include "HeapEnumerator.h"
#include "Dialogs.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"HeapMemView", IDR_MAIN_MENU)

    static constexpr int kDivider = 5;
    static constexpr uint32_t kSplitRange = 0x8000;

    CListViewCtrl m_list;
    CEdit m_edit;
    CFont m_editFont;
    CImageList m_images;

    std::vector<HeapBlock> m_items;
    Settings m_s;
    std::wstring m_cfgPath;
    DWORD m_pid = 0;
    std::wstring m_procName;
    int m_sortCol = 0;
    bool m_sortAsc = true;
    bool m_noSaveLoad = false;
    bool m_firstLoad = true;
    HACCEL m_hAccel = nullptr;
    HCURSOR m_splitCursor = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    bool m_dragging = false;
    int m_bandTop = 0, m_bandBottom = 0; // current pane band in client coords

    CMainFrame();

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETFOCUS(OnSetFocus)
    MSG_WM_SETCURSOR(OnSetCursor)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    MESSAGE_HANDLER(m_findMsg, OnFindReplace)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_CUSTOMDRAW, OnCustomDraw)
    COMMAND_ID_HANDLER(ID_FILE_SELECTPROC, OnSelectProcess)
    COMMAND_ID_HANDLER(ID_FILE_EXPORTDATA, OnExportData)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_EDIT_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_VIEW_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_VIEW_COLUMNS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_VIEW_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_VIEW_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_PREVIEW_NONE, OnPreviewMode)
    COMMAND_ID_HANDLER(ID_PREVIEW_ASCII, OnPreviewMode)
    COMMAND_ID_HANDLER(ID_PREVIEW_HEX, OnPreviewMode)
    COMMAND_ID_HANDLER(ID_OPT_DISPLAY_HEADER, OnToggleHeader)
    COMMAND_ID_HANDLER(ID_OPT_MARK_NEW, OnToggleMarkNew)
    COMMAND_ID_HANDLER(ID_OPT_ABOVE127, OnToggleAbove127)
    COMMAND_ID_HANDLER(ID_OPT_AUTOSORT, OnToggleAutoSort)
    COMMAND_ID_HANDLER(ID_OPT_CLEAR_NEW, OnClearNew)
    COMMAND_ID_HANDLER(ID_OPT_FILTER, OnFilter)
    COMMAND_ID_HANDLER(ID_OPT_ADVANCED, OnAdvanced)
    COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT type, CSize);
    int DividerY() const;
    bool OverDivider(int y) const;
    BOOL OnSetCursor(CWindow wnd, UINT, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);
    void LayoutStatusParts();
    void OnSetFocus(CWindow);
    void UpdateMenuChecks();

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void ApplyGridLines();

    // ---- data -----------------------------------------------------------
    HeapScanOptions ScanOptions() const;
    void Refresh();
    void SortItems();
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);
    void EnsurePreview(HeapBlock& b);
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnCustomDraw(int, LPNMHDR p, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
    void UpdateStatus();

    // ---- lower pane -----------------------------------------------------
    void UpdateLowerPane();

    // ---- selection helpers ---------------------------------------------
    std::vector<int> SelectedIndices();
    std::vector<HeapBlock> SelectedItems();
    int FocusedIndex();

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectProcess(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnPreviewMode(WORD, WORD id, HWND, BOOL&);
    LRESULT OnToggleHeader(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleMarkNew(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleAbove127(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleAutoSort(WORD, WORD, HWND, BOOL&);
    LRESULT OnClearNew(WORD, WORD, HWND, BOOL&);
    LRESULT OnFilter(WORD, WORD, HWND, BOOL&);
    LRESULT OnAdvanced(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    LRESULT OnExportData(WORD, WORD, HWND, BOOL&);
    void DumpBlock(const HeapBlock& b, const wchar_t* path);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    void ShowHtmlReport(const std::vector<HeapBlock>& items, bool horizontal);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnDblClk(int, LPNMHDR, BOOL&);
    LRESULT OnRClick(int, LPNMHDR, BOOL&);

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindReplace(UINT, WPARAM, LPARAM lParam, BOOL&);
    void FindNext(const std::wstring& needle, bool down);
    void CopyToClipboard(const std::wstring& text);

    // ---- config ---------------------------------------------------------
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
