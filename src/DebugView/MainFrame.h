// MainFrame.h -- WTL main window for the DebugView recreation.
//
// Layout follows the original: the LISTMENU menu, a 16px toolbar built from the
// original BITMAP 400 strip, a status bar, and a report list. The original
// draws its own output pane, this recreation uses an owner-data report ListView
// with the sequence, time, pid and message columns the task asks for. The Win32
// capture engine runs on its own thread and feeds rows through WM_DBG_LINE.
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
#include "DbgRow.h"
#include "Columns.h"
#include "Config.h"
#include "Capture.h"
#include "Export.h"
#include "Dialogs.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"dbgviewClass", IDR_MENU)

    CListViewCtrl m_list;
    std::vector<DbgRow> m_rows; // every captured line
    std::vector<int> m_view;    // indices of m_rows that pass the filter
    std::vector<int> m_visCols; // visible column ids in display order
    Config m_cfg;
    DbwinCapture m_capture;
    CFont m_font;

    unsigned m_nextSeq = 0;
    LARGE_INTEGER m_freq{};
    long long m_baseQpc = 0;
    bool m_haveBase = false;
    bool m_kernelWarned = false;

    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    std::wstring m_lastFind;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

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
    MESSAGE_HANDLER(WM_DBG_LINE, OnDbgLine)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_CUSTOMDRAW, OnCustomDraw)
    NOTIFY_CODE_HANDLER(TTN_GETDISPINFOW, OnToolTip)
    COMMAND_ID_HANDLER(ID_CAPTURE_EVENTS, OnCaptureEvents)
    COMMAND_ID_HANDLER(ID_CAPTURE_WIN32, OnCaptureWin32)
    COMMAND_ID_HANDLER(ID_CAPTURE_KERNEL, OnCaptureKernel)
    COMMAND_ID_HANDLER(ID_EDIT_CLEAR, OnClear)
    COMMAND_ID_HANDLER(ID_OPT_AUTOSCROLL, OnAutoScroll)
    COMMAND_ID_HANDLER(ID_OPT_CLOCKTIME, OnClockTime)
    COMMAND_ID_HANDLER(ID_OPT_MILLISECONDS, OnMilliseconds)
    COMMAND_ID_HANDLER(ID_OPT_WIN32PIDS, OnWin32Pids)
    COMMAND_ID_HANDLER(ID_OPT_HIDETOOLBAR, OnHideToolbar)
    COMMAND_ID_HANDLER(ID_OPT_ALWAYSONTOP, OnAlwaysOnTop)
    COMMAND_ID_HANDLER(ID_OPT_FONT, OnFont)
    COMMAND_ID_HANDLER(ID_OPT_HISTORY, OnHistory)
    COMMAND_ID_HANDLER(ID_EDIT_FILTER, OnFilter)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_FILE_SAVEAS, OnSave)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_EDIT_FINDNEXT, OnFindNext)
    COMMAND_ID_HANDLER(ID_EDIT_FINDPREV, OnFindPrev)
    COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void SyncWidths();
    void SyncMenuChecks();
    void SyncToolbar();
    void DisableUnsupported();

    // ---- capture --------------------------------------------------------
    void ApplyCapture();
    LRESULT OnDbgLine(UINT, WPARAM, LPARAM lParam, BOOL&);
    void TrimHistory();
    void RebuildView();

    // ---- cell formatting ------------------------------------------------
    std::wstring cell(const DbgRow& row, int col);
    std::wstring formatTime(const DbgRow& row);
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
    LRESULT OnCustomDraw(int, LPNMHDR p, BOOL&);
    LRESULT OnToolTip(int, LPNMHDR p, BOOL&);
    void UpdateStatus();

    // ---- capture commands ----------------------------------------------
    LRESULT OnCaptureEvents(WORD, WORD, HWND, BOOL&);
    LRESULT OnCaptureWin32(WORD, WORD, HWND, BOOL&);
    LRESULT OnCaptureKernel(WORD, WORD, HWND, BOOL&);
    LRESULT OnClear(WORD, WORD, HWND, BOOL&);

    // ---- option toggles -------------------------------------------------
    LRESULT OnAutoScroll(WORD, WORD, HWND, BOOL&);
    LRESULT OnClockTime(WORD, WORD, HWND, BOOL&);
    LRESULT OnMilliseconds(WORD, WORD, HWND, BOOL&);
    LRESULT OnWin32Pids(WORD, WORD, HWND, BOOL&);
    LRESULT OnHideToolbar(WORD, WORD, HWND, BOOL&);
    void ApplyToolbarVisibility();
    LRESULT OnAlwaysOnTop(WORD, WORD, HWND, BOOL&);
    void ApplyAlwaysOnTop();
    LRESULT OnFont(WORD, WORD, HWND, BOOL&);
    LRESULT OnHistory(WORD, WORD, HWND, BOOL&);
    LRESULT OnFilter(WORD, WORD, HWND, BOOL&);

    // ---- selection / clipboard / save ----------------------------------
    Report buildReport(bool selectedOnly);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    void CopyToClipboard(const std::wstring& text);

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindNext(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindPrev(WORD, WORD, HWND, BOOL&);
    void OnFindReplaceMsg(LPARAM lParam);
    void FindText(const std::wstring& what, bool down);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);

    void OnDestroy();
};
