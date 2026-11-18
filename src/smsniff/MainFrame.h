// MainFrame.h -- the WTL main window for the SmartSniff recreation. The original registers
// a plain window class "SmartSniff" and hangs a toolbar, a status bar, an upper
// SysListView32 (one row per conversation) and a lower pane off it, with a draggable
// horizontal splitter between them. This rebuilds that layout over the packet model.
//
// The upper list is owner-data. The lower pane shows the selected conversation as a hex
// dump or ascii stream. Live capture is intentionally not wired up (see PcapLoader.h and
// analysis/smsniff.md). Packets come from Load Packets Data From File.
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
#include <fstream>
#include "resource.h"
#include "../common/UiUtil.h"
#include "Columns.h"
#include "Config.h"
#include "Packet.h"
#include "Export.h"
#include "Dialogs.h"
#include "PcapLoader.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"SmartSniff", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    CEdit m_lower;

    Config m_cfg;
    bool m_noLoadSettings = false;
    PcapApi* m_pcap = nullptr;

    std::vector<SsPacket> m_packets;
    uint32_t m_captureIp = 0;
    std::vector<Connection> m_all;  // every parsed conversation
    std::vector<Connection> m_view; // the protocol-filtered, sorted view

    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    int m_upperH = 0; // upper pane height, drives the splitter
    bool m_dragging = false;
    static const int kSplitBar = 6;

    DisplayOptions disp() const
    {
        DisplayOptions d;
        d.showOutgoingIncoming = m_cfg.showOutgoingIncoming;
        return d;
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
    NOTIFY_CODE_HANDLER(NM_RCLICK, OnRClick)
    COMMAND_ID_HANDLER(ID_LOAD_PACKETS, OnLoadPackets)
    COMMAND_ID_HANDLER(ID_SAVE_PACKETS, OnSavePackets)
    COMMAND_ID_HANDLER(ID_SAVE_SUMMARIES, OnSaveSummaries)
    COMMAND_ID_HANDLER(ID_HTML_REPORT, OnHtmlReport)
    COMMAND_ID_HANDLER(ID_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_SELECT_ALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_DESELECT_ALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_CHOOSE_COLUMNS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_AUTOSIZE_HDR, OnAutoSizeHdr)
    COMMAND_ID_HANDLER(ID_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_FIND_UPPER, OnFind)
    COMMAND_ID_HANDLER(ID_MOVE_NEXT, OnMoveNext)
    COMMAND_ID_HANDLER(ID_MOVE_PREV, OnMovePrev)
    COMMAND_ID_HANDLER(ID_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_TOOLTIPS, OnTooltips)
    COMMAND_ID_HANDLER(ID_MARK_ODDEVEN, OnMarkOddEven)
    COMMAND_ID_HANDLER(ID_OUTGOING_INCOMING, OnOutgoingIncoming)
    COMMAND_ID_HANDLER(ID_RESOLVE_IP, OnResolveIp)
    COMMAND_ID_HANDLER(ID_ALWAYS_ON_TOP, OnAlwaysOnTop)
    COMMAND_ID_HANDLER(ID_HIDE_LOWER, OnHideLower)
    COMMAND_ID_HANDLER(ID_MODE_AUTO, OnDisplayMode)
    COMMAND_ID_HANDLER(ID_MODE_ASCII, OnDisplayMode)
    COMMAND_ID_HANDLER(ID_MODE_HEX, OnDisplayMode)
    COMMAND_ID_HANDLER(ID_MODE_URL, OnDisplayMode)
    COMMAND_ID_HANDLER(ID_PROTO_TCP, OnProtoFilter)
    COMMAND_ID_HANDLER(ID_PROTO_UDP, OnProtoFilter)
    COMMAND_ID_HANDLER(ID_PROTO_ICMP, OnProtoFilter)
    COMMAND_ID_HANDLER(ID_CAPTURE_OPTIONS, OnCaptureOptions)
    COMMAND_ID_HANDLER(ID_START_CAPTURE, OnStartCapture)
    COMMAND_ID_HANDLER(ID_STOP_CAPTURE, OnStopCapture)
    COMMAND_ID_HANDLER(ID_RESTART_CAPTURE, OnStartCapture)
    COMMAND_ID_HANDLER(ID_SAVE_CONFIG, OnSaveConfig)
    COMMAND_ID_HANDLER(ID_LOAD_CONFIG, OnLoadConfig)
    COMMAND_ID_HANDLER(ID_CAPTURE_FILTER, OnCaptureFilter)
    COMMAND_ID_HANDLER(ID_DISPLAY_FILTER, OnDisplayFilter)
    COMMAND_ID_HANDLER(ID_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();

    // ---- layout ---------------------------------------------------------
    int toolbarHeight();
    int statusHeight();
    void OnSize(UINT, CSize);
    CRect splitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hitTest, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void ApplyGridLines();
    void ApplyAlwaysOnTop();

    // ---- data -----------------------------------------------------------
    void Rebuild();
    void ApplyFilterAndSort();
    void SortView();
    void LoadPacketsFromFile(const std::wstring& path);

    // ---- lower pane rendering ------------------------------------------
    void RenderLower();

    // ---- owner-data -----------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
    LRESULT OnRClick(int, LPNMHDR, BOOL&);

    // ---- status ---------------------------------------------------------
    void UpdateStatus();

    // ---- export helpers -------------------------------------------------
    std::vector<int> selectedRows();
    ExportRequest buildRequest();

    // ---- commands -------------------------------------------------------
    LRESULT OnLoadPackets(WORD, WORD, HWND, BOOL&);
    LRESULT OnSavePackets(WORD, WORD, HWND, BOOL&);
    LRESULT OnSaveSummaries(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlReport(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    void AutoSize(int how);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&);
    LRESULT OnMoveNext(WORD, WORD, HWND, BOOL&);
    LRESULT OnMovePrev(WORD, WORD, HWND, BOOL&);
    void MoveSel(int delta);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&);
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnOutgoingIncoming(WORD, WORD, HWND, BOOL&);
    LRESULT OnResolveIp(WORD, WORD, HWND, BOOL&);
    LRESULT OnAlwaysOnTop(WORD, WORD, HWND, BOOL&);
    LRESULT OnHideLower(WORD, WORD, HWND, BOOL&);
    LRESULT OnDisplayMode(WORD, WORD id, HWND, BOOL&);
    LRESULT OnProtoFilter(WORD, WORD id, HWND, BOOL&);
    LRESULT OnCaptureOptions(WORD, WORD, HWND, BOOL&);
    LRESULT OnStartCapture(WORD, WORD, HWND, BOOL&);
    LRESULT OnStopCapture(WORD, WORD, HWND, BOOL&);
    LRESULT OnSaveConfig(WORD, WORD, HWND, BOOL&);
    LRESULT OnLoadConfig(WORD, WORD, HWND, BOOL&);
    LRESULT OnCaptureFilter(WORD, WORD, HWND, BOOL&);
    LRESULT OnDisplayFilter(WORD, WORD, HWND, BOOL&);
    void EditFilter(std::wstring& target, const wchar_t* title);

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    void OnFindReplaceMsg(LPARAM lParam);
    void FindText(const std::wstring& what, bool down);

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);

    void SyncChecks();
    void CopyToClipboard(const std::wstring& text);
    void OnDestroy();

  private:
    HFONT m_lowerFont = nullptr;
};
