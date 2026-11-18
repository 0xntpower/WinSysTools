// MainFrame.h -- the Process Explorer main window.
//
// A toolbar and status bar frame the two stacked panes split by a draggable
// bar. The upper pane is the process tree as an indented report list: each row
// carries the process icon, and the Process, PID, CPU, Private Bytes, Working
// Set, Description and Company Name columns. A one second timer refreshes it,
// colouring own, service and suspended processes and flashing new processes
// green and exited processes red. The lower pane shows the selected process's
// DLLs or handles. The frame is pinned to light mode with the shared toolbar
// fix so the band reads light on a dark-mode system.
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
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "ProcessData.h"
#include "Modules.h"
#include "Handles.h"
#include "Dialogs.h"
#include "Eula.h"

extern CAppModule _Module;

namespace pe
{
    // Row background colours. These follow the original's default scheme, the
    // exact registry-stored defaults (ColorOwn, ColorServices and the rest) are
    // honoured at runtime when present but are set by the original at runtime,
    // so the light-mode scheme is reproduced here rather than byte-extracted.
    inline constexpr COLORREF kColorOwn = RGB(0xCC, 0xE8, 0xFF);       // light blue
    inline constexpr COLORREF kColorServices = RGB(0xFF, 0xE1, 0xE1);  // light pink
    inline constexpr COLORREF kColorSuspended = RGB(0xC4, 0xC4, 0xC4); // gray
    inline constexpr COLORREF kColorNew = RGB(0xDF, 0xFF, 0xDF);       // green flash
    inline constexpr COLORREF kColorDead = RGB(0xFF, 0xD0, 0xD0);      // red flash

    enum class LowerMode
    {
        Dlls,
        Handles
    };
} // namespace pe

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
    DECLARE_FRAME_WND_CLASS(L"PROCEXPLORER", IDR_MAINFRAME)

    CListViewCtrl m_tree;  // upper pane: process tree-list
    CListViewCtrl m_lower; // lower pane: DLLs or handles
    CImageList m_icons;

    pe::Engine m_engine;
    std::vector<pe::ProcRow> m_rows; // flattened display order
    std::vector<int> m_depth;        // per display row tree depth
    std::unordered_map<DWORD, pe::ProcRow> m_prevByPid;
    std::unordered_set<DWORD> m_prevPids;
    std::unordered_map<DWORD, uint64_t> m_newFlash; // pid -> flash-until
    std::vector<pe::ProcRow> m_deadRows;            // exited processes still flashing
    std::unordered_map<std::wstring, int> m_iconMap;

    pe::LowerMode m_lowerMode = pe::LowerMode::Dlls;
    bool m_showLower = true;
    UINT m_intervalMs = 1000;
    bool m_paused = false;
    uint64_t m_highlightMs = 1000;

    int m_splitY = 0;
    bool m_dragging = false;
    static constexpr int kSplitBar = 5;

    HACCEL m_hAccel = nullptr;
    CFont m_font;
    LOGFONTW m_logFont{};
    bool m_haveFont = false;

    WINDOWPLACEMENT m_placement{};
    bool m_havePlacement = false;

    static constexpr UINT_PTR kRefreshTimer = 1;
    enum
    {
        kColCount = 7
    };

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_TIMER(OnTimer)
    MSG_WM_SETCURSOR(OnSetCursor)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
    NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnTreeItemChanged)
    NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClk)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_PROC_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_PROC_KILL, OnKill)
    COMMAND_ID_HANDLER(ID_PROC_SUSPEND, OnSuspend)
    COMMAND_ID_HANDLER(ID_PROC_SEARCHONLINE, OnSearchOnline)
    COMMAND_ID_HANDLER(ID_PRI_REALTIME, OnSetPriority)
    COMMAND_ID_HANDLER(ID_PRI_HIGH, OnSetPriority)
    COMMAND_ID_HANDLER(ID_PRI_NORMAL, OnSetPriority)
    COMMAND_ID_HANDLER(ID_PRI_IDLE, OnSetPriority)
    COMMAND_ID_HANDLER(ID_VIEW_DLLS, OnLowerDlls)
    COMMAND_ID_HANDLER(ID_VIEW_HANDLES, OnLowerHandles)
    COMMAND_ID_HANDLER(ID_VIEW_LOWERPANE, OnToggleLower)
    COMMAND_ID_HANDLER(ID_SPEED_HALF, OnSpeed)
    COMMAND_ID_HANDLER(ID_SPEED_1, OnSpeed)
    COMMAND_ID_HANDLER(ID_SPEED_2, OnSpeed)
    COMMAND_ID_HANDLER(ID_SPEED_5, OnSpeed)
    COMMAND_ID_HANDLER(ID_SPEED_10, OnSpeed)
    COMMAND_ID_HANDLER(ID_SPEED_PAUSED, OnPaused)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_FILE_SAVE_AS, OnSave)
    COMMAND_ID_HANDLER(ID_FIND_HANDLEDLL, OnFindHandle)
    COMMAND_ID_HANDLER(ID_OPT_FONT, OnFont)
    COMMAND_ID_HANDLER(ID_HELP, OnHelp)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    static std::wstring BuildTitle();
    void CreateToolbar();
    void BuildTreeColumns();
    void BuildLowerColumns();
    void AddGenericIcon();
    int IconFor(const std::wstring& path);

    // ---- refresh --------------------------------------------------------
    void OnTimer(UINT_PTR id);
    void Refresh();
    void FlattenTree(std::vector<pe::ProcRow>& live);
    void InsertTreeRow(int i);
    static std::wstring FormatKb(uint64_t bytes);
    DWORD SelectedPid();
    void RestoreSelection(DWORD selPid, DWORD topPid);

    // ---- colouring ------------------------------------------------------
    LRESULT OnCustomDraw(int, LPNMHDR p, BOOL& handled);
    static COLORREF RowColor(const pe::ProcRow& r);

    // ---- lower pane -----------------------------------------------------
    LRESULT OnTreeItemChanged(int, LPNMHDR p, BOOL& handled);
    void UpdateLowerPane();

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT, CSize);
    int BandHeight(HWND h);
    CRect SplitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hit, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);
    void UpdateStatus(int count);

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    const pe::ProcRow* SelectedRow();
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnKill(WORD, WORD, HWND, BOOL&);
    LRESULT OnSuspend(WORD, WORD, HWND, BOOL&);
    LRESULT OnSetPriority(WORD, WORD id, HWND, BOOL&);
    LRESULT OnSearchOnline(WORD, WORD, HWND, BOOL&);
    LRESULT OnDblClk(int, LPNMHDR p, BOOL&);
    LRESULT OnLowerDlls(WORD, WORD, HWND, BOOL&);
    LRESULT OnLowerHandles(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleLower(WORD, WORD, HWND, BOOL&);
    LRESULT OnSpeed(WORD, WORD id, HWND, BOOL&);
    LRESULT OnPaused(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    void SaveTreeToFile(const wchar_t* path);
    LRESULT OnFindHandle(WORD, WORD, HWND, BOOL&);
    LRESULT OnFont(WORD, WORD, HWND, BOOL&);
    void ApplyFont();
    LRESULT OnHelp(WORD, WORD, HWND, BOOL&);
    void SyncMenu();

    // ---- config (registry) ---------------------------------------------
    static constexpr const wchar_t* kKey = L"Software\\Sysinternals\\Process Explorer";
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
