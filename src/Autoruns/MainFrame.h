// MainFrame.h -- the Autoruns main window.
//
// The original is an MFC frame holding a tab bar over a report list, one tab per
// autostart category plus an Everything aggregate. This rebuilds that as a
// CFrameWindowImpl with a de-themed toolbar (pinned light), a tab control and a
// single report list view that is repopulated for the selected tab. Each row
// carries the Autorun Entry, Description, Publisher, Image Path, Timestamp and
// the enabled checkbox. Authenticode verification runs on a background thread
// and refreshes the Publisher column and the Microsoft and Windows filters when
// it finishes, exactly as the original defers verification off the UI thread.
//
// Everything here is read-only. No menu path deletes, disables or edits a real
// autorun entry. The Delete command is deliberately inert.
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
#include <map>
#include <thread>
#include <atomic>
#include <algorithm>
#include <shlobj.h>
#include "resource.h"
#include "../common/UiUtil.h"
#include "AutorunEntry.h"
#include "Enumerators.h"
#include "Signature.h"
#include "Dialogs.h"
#include "Eula.h"

extern CAppModule _Module;

#define WM_APP_VERIFY_DONE (WM_APP + 1)

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
    DECLARE_FRAME_WND_CLASS(L"AutorunsClass", IDR_MAINFRAME)

    CToolBarCtrl m_toolbar;
    CStatusBarCtrl m_status;
    CTabCtrl m_tabs;
    CListViewCtrl m_list;
    HIMAGELIST m_tbImages = nullptr;
    CFont m_boldFont;

    std::vector<ar::Location> m_locations;

    // One display row: a bold location header, or an entry under it.
    struct Row
    {
        bool header;
        int loc;
        int entry;
    };
    std::vector<Row> m_rows;

    bool m_hideWindows = false;
    bool m_hideMicrosoft = false;
    bool m_hideEmpty = false;
    bool m_alwaysOnTop = false;

    std::wstring m_winDir;

    HACCEL m_hAccel = nullptr;
    WINDOWPLACEMENT m_placement{};
    bool m_havePlacement = false;

    std::thread m_verify;
    std::atomic<bool> m_stop{false};
    std::atomic<unsigned> m_scanGen{0};

    UINT m_findMsg = 0;
    CFindReplaceDialog* m_findDlg = nullptr;
    std::wstring m_lastFind;

    // Tab 0 is Everything, tabs 1.. map to the categories in enum order.
    static constexpr int kTabCount = 1 + static_cast<int>(ar::Category::Count);

    // Config lives under this registry key, matching the original.
    static constexpr const wchar_t* kKey = L"Software\\Sysinternals\\Autoruns";

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MESSAGE_HANDLER(WM_APP_VERIFY_DONE, OnVerifyDone)
    MESSAGE_HANDLER(m_findMsg, OnFindMsg)
    NOTIFY_HANDLER(IDC_TABS, TCN_SELCHANGE, OnTabChange)
    NOTIFY_HANDLER(IDC_LIST, NM_CUSTOMDRAW, OnCustomDraw)
    NOTIFY_HANDLER(IDC_LIST, NM_DBLCLK, OnDblClick)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_HELP_AUTORUNS, OnHelp)
    COMMAND_ID_HANDLER(ID_HIDE_WINDOWS, OnHideWindows)
    COMMAND_ID_HANDLER(ID_HIDE_MICROSOFT, OnHideMicrosoft)
    COMMAND_ID_HANDLER(ID_HIDE_EMPTY, OnHideEmpty)
    COMMAND_ID_HANDLER(ID_ALWAYS_ONTOP, OnAlwaysOnTop)
    COMMAND_ID_HANDLER(ID_JUMP_ENTRY, OnJumpEntry)
    COMMAND_ID_HANDLER(ID_JUMP_IMAGE, OnJumpImage)
    COMMAND_ID_HANDLER(ID_VERIFY_IMAGE, OnVerifyImage)
    COMMAND_ID_HANDLER(ID_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_QUICK_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_FIND_NEXT, OnFindNext)
    COMMAND_ID_HANDLER(ID_EDIT_CLEAR, OnReadOnly)
    COMMAND_ID_HANDLER(ID_FILE_OPEN, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_ANALYZE_OFFLINE, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_COMPARE, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_CHECK_VT, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_SEARCH_ONLINE, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_PROCEXP, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_SCAN_OPTIONS, OnOutOfScope)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void BuildColumns();
    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- scan and verify ------------------------------------------------
    void Scan();
    void StartVerify();
    void StopVerify();
    LRESULT OnVerifyDone(UINT, WPARAM wParam, LPARAM lParam, BOOL&);

    // ---- filters --------------------------------------------------------
    [[nodiscard]] bool IsMicrosoftEntry(const ar::AutorunEntry& e) const;
    [[nodiscard]] bool IsWindowsEntry(const ar::AutorunEntry& e) const;
    [[nodiscard]] bool EntryVisible(const ar::AutorunEntry& e) const;

    // ---- list build -----------------------------------------------------
    void RebuildList();
    int IconIndex(const std::wstring& path);
    LRESULT OnCustomDraw(int, LPNMHDR p, BOOL&);
    LRESULT OnDblClick(int, LPNMHDR, BOOL&);

    // ---- row helpers ----------------------------------------------------
    const ar::AutorunEntry* FocusedEntry();
    const ar::Location* FocusedLocation();

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnHelp(WORD, WORD, HWND, BOOL&);
    LRESULT OnHideWindows(WORD, WORD, HWND, BOOL&);
    LRESULT OnHideMicrosoft(WORD, WORD, HWND, BOOL&);
    LRESULT OnHideEmpty(WORD, WORD, HWND, BOOL&);
    LRESULT OnAlwaysOnTop(WORD, WORD, HWND, BOOL&);
    LRESULT OnTabChange(int, LPNMHDR, BOOL&);
    void DoJumpEntry();
    LRESULT OnJumpEntry(WORD, WORD, HWND, BOOL&);
    LRESULT OnJumpImage(WORD, WORD, HWND, BOOL&);
    LRESULT OnVerifyImage(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnReadOnly(WORD, WORD, HWND, BOOL&);
    LRESULT OnOutOfScope(WORD, WORD, HWND, BOOL&);

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindNext(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindMsg(UINT, WPARAM, LPARAM lParam, BOOL&);
    void DoFind(const std::wstring& needle);
    void CopyToClipboard(const std::wstring& text);
    void SetStatus(const wchar_t* text);
    void SyncMenu();

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT wParam, CSize);

    // ---- config (registry) ----------------------------------------------
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
