// MainFrame.h -- the VMMap main window.
//
// The original hangs a header (process name, PID, committed, private and
// working-set totals) above two multi-column panes off a dialog-template
// window of class VmMapClass: a summary pane of memory categories on top and a
// detail pane of individual regions below, split by a draggable bar. This
// rebuilds that over the snapshot engine, using two report-mode list views in
// place of the original's custom multi-column trees, pinned to light mode. The
// bottom Trace, Call Tree, Heap Allocations and Timeline buttons belong to the
// launch-and-trace feature and are shown disabled, out of scope.
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
#include <string>
#include <vector>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "MemSnapshot.h"
#include "Export.h"
#include "Dialogs.h"
#include "Eula.h"

extern CAppModule _Module;

struct ScopedWait
{
    HCURSOR prev;
    ScopedWait()
        : prev(::SetCursor(::LoadCursorW(nullptr, IDC_WAIT)))
    {
    }
    ~ScopedWait()
    {
        ::SetCursor(prev);
    }
};

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"VmMapClass", IDR_MAIN_MENU)

    CListViewCtrl m_summary;
    CListViewCtrl m_detail;

    // Header labels and values.
    CStatic m_hIcon;
    CStatic m_procName, m_procPid, m_committed, m_private, m_ws;

    // Bottom out-of-scope buttons.
    CButton m_btnTimeline, m_btnHeap, m_btnCallTree, m_btnTrace;

    vm::Snapshot m_snap;
    std::vector<int> m_detailRows; // indices into m_snap.regions after filtering
    bool m_showFree = false;

    int m_splitY = 0; // detail-pane top, measured from middle area
    bool m_dragging = false;
    static const int kSplitBar = 5;
    static const int kHeaderH = 92;
    static const int kFooterH = 28;

    HACCEL m_hAccel = nullptr;
    CFont m_font;
    LOGFONTW m_logFont{};
    bool m_haveFont = false;

    UINT m_findMsg = 0;
    CFindReplaceDialog* m_findDlg = nullptr;
    std::wstring m_lastFind;

    WINDOWPLACEMENT m_placement{};
    bool m_havePlacement = false;

    enum
    {
        kSumCols = 11,
        kDetCols = 13
    };

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETCURSOR(OnSetCursor)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    MESSAGE_HANDLER(m_findMsg, OnFindMsg)
    NOTIFY_CODE_HANDLER(LVN_GETDISPINFOW, OnGetDispInfo)
    COMMAND_ID_HANDLER(ID_FILE_SELECTPROC, OnSelectProcess)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSaveAs)
    COMMAND_ID_HANDLER(ID_FILE_SAVEAS, OnSaveAs)
    COMMAND_ID_HANDLER(ID_EDIT_COPYADDR, OnCopyAddr)
    COMMAND_ID_HANDLER(ID_EDIT_COPYALL, OnCopyAll)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_FIND_NEXT, OnFindNext)
    COMMAND_ID_HANDLER(ID_TOOLS_EMPTYWS, OnEmptyWs)
    COMMAND_ID_HANDLER(ID_OPT_SHOWFREE, OnShowFree)
    COMMAND_ID_HANDLER(ID_OPT_FONT, OnFont)
    COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_HELP_QUICK, OnQuickHelp)
    COMMAND_ID_HANDLER(ID_HELP_CMDLINE, OnCmdLine)
    COMMAND_ID_HANDLER(ID_HELP_HELP, OnHelp)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateHeader();
    void CreateFooter();
    void BuildSummaryColumns();
    void BuildDetailColumns();

    // ---- formatting -----------------------------------------------------
    static std::wstring FormatKb(uint64_t bytes);
    static std::wstring FormatCount(uint32_t n);
    static std::wstring Group(const std::wstring& digits);
    static std::wstring FormatAddr(uint64_t a);

    // ---- snapshot -------------------------------------------------------
    void TakeSnapshot(DWORD pid, const std::wstring& name);
    void UpdateHeader();
    void PopulateSummary();
    void SetSum(int row, int col, uint64_t v);
    void RebuildDetail();
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT, CSize);
    void LayoutHeader(const CRect& rc);
    void LayoutFooter(const CRect& rc);
    CRect SplitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hit, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);

    // ---- commands -------------------------------------------------------
    LRESULT OnSelectProcess(WORD, WORD, HWND, BOOL&);
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnShowFree(WORD, WORD, HWND, BOOL&);
    LRESULT OnEmptyWs(WORD, WORD, HWND, BOOL&);
    LRESULT OnFont(WORD, WORD, HWND, BOOL&);
    void ApplyFont();
    LRESULT OnCopyAddr(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnSaveAs(WORD, WORD, HWND, BOOL&);
    std::wstring BuildExport(wchar_t sep);
    void CopyText(const std::wstring& text);

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindNext(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindMsg(UINT, WPARAM, LPARAM lParam, BOOL&);
    void DoFind(const std::wstring& needle);

    LRESULT OnQuickHelp(WORD, WORD, HWND, BOOL&);
    LRESULT OnCmdLine(WORD, WORD, HWND, BOOL&);
    LRESULT OnHelp(WORD, WORD, HWND, BOOL&);
    static INT_PTR CALLBACK UsageProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM);

    void SyncMenu();

    // ---- config (registry) ---------------------------------------------
    static constexpr const wchar_t* kKey = L"Software\\Sysinternals\\VMMap";
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
