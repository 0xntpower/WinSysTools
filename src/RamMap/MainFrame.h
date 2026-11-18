// MainFrame.h -- the RamMap main window.
//
// The original is a dialog-template window of class RamMapClass holding a single
// tab control (id 1037) whose seven tabs (Use Counts, Processes, Priority
// Summary, Physical Pages, Physical Ranges, File Summary, File Details) are
// child dialogs created from the LISTVIEW*, SUMMARY and TREELIST templates. This
// rebuilds that as a CFrameWindowImpl of the same class name holding a WTL tab
// control, with a report-mode list view (or a tree) per tab, pinned to light
// mode. RamMap has no toolbar band, so the guide's toolbar light fix does not
// apply.
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
#include "MemInfo.h"
#include "Dialogs.h"
#include "Eula.h"

extern CAppModule _Module;

struct ScopedWait
{
    HCURSOR prev;
    ScopedWait() : prev(::SetCursor(::LoadCursorW(nullptr, IDC_WAIT))) {}
    ~ScopedWait() { ::SetCursor(prev); }
};

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"RamMapClass", IDR_MAIN_MENU)

    enum Tab { kUseCounts = 0, kProcesses, kPriority, kPhysPages, kPhysRanges, kFileSummary, kFileDetails, kTabCount };

    CTabCtrl       m_tabs;
    CListViewCtrl  m_useCounts;
    CListViewCtrl  m_processes;
    CListViewCtrl  m_priority;
    CListViewCtrl  m_ranges;
    CListViewCtrl  m_fileSummary;
    CTreeViewCtrl  m_fileDetails;
    CStatic        m_physNote;

    ram::Snapshot  m_snap;
    HACCEL         m_hAccel = nullptr;
    int            m_cur = kUseCounts;

    WINDOWPLACEMENT m_placement{};
    bool m_havePlacement = false;

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
        {
            return TRUE;
        }
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        NOTIFY_HANDLER(IDC_TABS, TCN_SELCHANGE, OnTabChange)
        COMMAND_ID_HANDLER(ID_FILE_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_FILE_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_FILE_OPEN, OnOpenSave)
        COMMAND_ID_HANDLER(ID_FILE_SAVE, OnOpenSave)
        COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
        COMMAND_ID_HANDLER(ID_EMPTY_WORKINGSETS, OnEmpty)
        COMMAND_ID_HANDLER(ID_EMPTY_SYSTEMWS, OnEmpty)
        COMMAND_ID_HANDLER(ID_EMPTY_MODIFIED, OnEmpty)
        COMMAND_ID_HANDLER(ID_EMPTY_STANDBY, OnEmpty)
        COMMAND_ID_HANDLER(ID_EMPTY_PRIORITY0, OnEmpty)
        COMMAND_ID_HANDLER(ID_SETTINGS_COLORS, OnColors)
        COMMAND_ID_HANDLER(ID_HELP_USAGE, OnUsage)
        COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_PANE_NEXT, OnPaneNext)
        COMMAND_ID_HANDLER(ID_PANE_PREV, OnPanePrev)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        LoadConfig();

        m_tabs.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS, 0, IDC_TABS);
        m_tabs.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
        const wchar_t* tabNames[kTabCount] = {
            L"Use Counts", L"Processes", L"Priority Summary", L"Physical Pages",
            L"Physical Ranges", L"File Summary", L"File Details" };
        for (int i = 0; i < kTabCount; ++i)
        {
            m_tabs.InsertItem(i, tabNames[i]);
        }

        CreateChildren();
        BuildColumns();

        if (HICON hi = static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(),
            L"APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        SetWindowTextW(L"RamMap - Sysinternals: www.sysinternals.com");

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
        {
            pLoop->AddMessageFilter(this);
        }

        if (m_havePlacement)
        {
            SetWindowPlacement(&m_placement);
        }

        Refresh();
        ShowTab(kUseCounts);
        return 0;
    }

    void CreateChildren()
    {
        const DWORD lvStyle = WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_ALIGNLEFT;
        m_useCounts.Create(m_hWnd, rcDefault, nullptr, lvStyle, 0, IDC_USECOUNTS_LIST);
        m_processes.Create(m_hWnd, rcDefault, nullptr, lvStyle, 0, IDC_PROCESS_LIST);
        m_priority.Create(m_hWnd, rcDefault, nullptr, lvStyle, 0, IDC_PRIORITY_LIST);
        m_ranges.Create(m_hWnd, rcDefault, nullptr, lvStyle, 0, IDC_RANGES_LIST);
        m_fileSummary.Create(m_hWnd, rcDefault, nullptr, lvStyle, 0, IDC_FILESUMMARY_LIST);
        m_fileDetails.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT, 0, IDC_FILEDETAILS_TREE);
        m_physNote.Create(m_hWnd, rcDefault,
            L"Per-physical-page ownership (the exact owner of each page) comes from the "
            L"RAMMap.sys helper driver, which is out of scope in this user-mode recreation. "
            L"The paging-list state totals it shares with the other tabs are on the Use Counts tab.",
            WS_CHILD | SS_LEFT, 0, IDC_PHYSPAGES_NOTE);

        for (CListViewCtrl* lv : { &m_useCounts, &m_processes, &m_priority, &m_ranges, &m_fileSummary })
        {
            lv->SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
            lv->SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
        }
        m_fileDetails.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
        m_physNote.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
    }

    void BuildColumns()
    {
        // Use Counts: type label plus the nine paging-list state columns.
        m_useCounts.InsertColumn(0, L"Usage", LVCFMT_LEFT, 112);
        for (int c = 0; c < ram::kStateCount; ++c)
        {
            m_useCounts.InsertColumn(c + 1, ram::StateName(c), LVCFMT_RIGHT, 78);
        }

        // Priority Summary: priority, standby, repurposed.
        m_priority.InsertColumn(0, L"Priority", LVCFMT_LEFT, 90);
        m_priority.InsertColumn(1, L"Standby", LVCFMT_RIGHT, 110);
        m_priority.InsertColumn(2, L"Repurposed", LVCFMT_RIGHT, 110);

        // Physical Ranges: start, end, size.
        m_ranges.InsertColumn(0, L"Start", LVCFMT_LEFT, 170);
        m_ranges.InsertColumn(1, L"End", LVCFMT_LEFT, 170);
        m_ranges.InsertColumn(2, L"Size", LVCFMT_RIGHT, 110);

        // Processes: name, id, session, then the use-type page columns.
        m_processes.InsertColumn(0, L"Process", LVCFMT_LEFT, 180);
        m_processes.InsertColumn(1, L"ID", LVCFMT_RIGHT, 60);
        m_processes.InsertColumn(2, L"Session", LVCFMT_RIGHT, 60);
        m_processes.InsertColumn(3, L"Private", LVCFMT_RIGHT, 90);
        m_processes.InsertColumn(4, L"Standby", LVCFMT_RIGHT, 90);

        // File Summary: file path and cached sizes.
        m_fileSummary.InsertColumn(0, L"File", LVCFMT_LEFT, 360);
        m_fileSummary.InsertColumn(1, L"Total", LVCFMT_RIGHT, 90);
        m_fileSummary.InsertColumn(2, L"Standby", LVCFMT_RIGHT, 90);
        m_fileSummary.InsertColumn(3, L"Modified", LVCFMT_RIGHT, 90);
    }

    // ---- formatting -----------------------------------------------------
    static std::wstring Group(const std::wstring& digits)
    {
        std::wstring g;
        int c = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        {
            if (c && c % 3 == 0)
            {
                g.push_back(L',');
            }
            g.push_back(*it);
            ++c;
        }
        std::reverse(g.begin(), g.end());
        return g;
    }
    static std::wstring Kb(uint64_t bytes)
    {
        if (bytes == 0)
        {
            return std::wstring();
        }
        const uint64_t kb = (bytes + 1023) / 1024;
        wchar_t raw[32] = {};
        swprintf(raw, 32, L"%llu", static_cast<unsigned long long>(kb));
        return Group(raw) + L" K";
    }
    static std::wstring Hex(uint64_t a)
    {
        wchar_t buf[24] = {};
        swprintf(buf, 24, L"0x%016llX", static_cast<unsigned long long>(a));
        return buf;
    }

    // ---- data -----------------------------------------------------------
    void Refresh()
    {
        ScopedWait wc;
        m_snap = ram::Collect();
        PopulateUseCounts();
        PopulatePriority();
        PopulateRanges();
        // Processes, File Summary and File Details need the Superfetch
        // private-source enumeration and the FileInfo driver. They stay empty.
        m_processes.DeleteAllItems();
        m_fileSummary.DeleteAllItems();
        m_fileDetails.DeleteAllItems();
        m_fileDetails.InsertItem(
            L"Cached file detail comes from the FileInfo driver and the Superfetch "
            L"file enumeration, out of scope in this recreation.", TVI_ROOT, TVI_LAST);
    }

    void SetCell(CListViewCtrl& lv, int row, int col, const std::wstring& text)
    {
        lv.SetItemText(row, col, text.c_str());
    }

    void PopulateUseCounts()
    {
        m_useCounts.SetRedraw(FALSE);
        m_useCounts.DeleteAllItems();
        int row = 0;
        for (int t = 0; t < ram::kTypeCount; ++t)
        {
            m_useCounts.InsertItem(row, ram::TypeName(t));
            for (int c = 0; c < ram::kStateCount; ++c)
            {
                SetCell(m_useCounts, row, c + 1, Kb(m_snap.types[t].v[c]));
            }
            ++row;
        }
        // The aggregate Total row.
        m_useCounts.InsertItem(row, L"Total");
        for (int c = 0; c < ram::kStateCount; ++c)
        {
            SetCell(m_useCounts, row, c + 1, Kb(m_snap.total.v[c]));
        }
        m_useCounts.SetRedraw(TRUE);
    }

    void PopulatePriority()
    {
        m_priority.SetRedraw(FALSE);
        m_priority.DeleteAllItems();
        int row = 0;
        for (int p = 0; p < 8; ++p)
        {
            wchar_t name[8] = {};
            swprintf(name, 8, L"%d", p);
            m_priority.InsertItem(row, name);
            SetCell(m_priority, row, 1, Kb(m_snap.standbyByPrio[p]));
            SetCell(m_priority, row, 2, Kb(m_snap.repurposedByPrio[p]));
            ++row;
        }
        m_priority.InsertItem(row, L"Total");
        SetCell(m_priority, row, 1, Kb(m_snap.standbyTotal));
        SetCell(m_priority, row, 2, Kb(m_snap.repurposedTotal));
        m_priority.SetRedraw(TRUE);
    }

    void PopulateRanges()
    {
        m_ranges.SetRedraw(FALSE);
        m_ranges.DeleteAllItems();
        int row = 0;
        for (const ram::Range& r : m_snap.ranges)
        {
            m_ranges.InsertItem(row, Hex(r.start).c_str());
            SetCell(m_ranges, row, 1, Hex(r.start + r.size));
            SetCell(m_ranges, row, 2, Kb(r.size));
            ++row;
        }
        m_ranges.InsertItem(row, L"Total");
        SetCell(m_ranges, row, 2, Kb(m_snap.rangesTotal));
        m_ranges.SetRedraw(TRUE);
    }

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT, CSize)
    {
        CRect rc;
        GetClientRect(&rc);
        rc.DeflateRect(6, 6);
        m_tabs.SetWindowPos(nullptr, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER);

        CRect disp = rc;
        m_tabs.AdjustRect(FALSE, &disp);
        disp.DeflateRect(2, 2);
        for (HWND h : ChildHandles())
        {
            ::SetWindowPos(h, nullptr, disp.left, disp.top, disp.Width(), disp.Height(), SWP_NOZORDER);
        }
    }

    std::vector<HWND> ChildHandles()
    {
        return { m_useCounts.m_hWnd, m_processes.m_hWnd, m_priority.m_hWnd,
                 m_physNote.m_hWnd, m_ranges.m_hWnd, m_fileSummary.m_hWnd,
                 m_fileDetails.m_hWnd };
    }

    void ShowTab(int tab)
    {
        m_cur = tab;
        m_tabs.SetCurSel(tab);
        std::vector<HWND> h = ChildHandles();
        for (int i = 0; i < static_cast<int>(h.size()); ++i)
        {
            ::ShowWindow(h[i], i == tab ? SW_SHOW : SW_HIDE);
        }
    }

    LRESULT OnTabChange(int, LPNMHDR, BOOL&)
    {
        ShowTab(m_tabs.GetCurSel());
        return 0;
    }

    LRESULT OnPaneNext(WORD, WORD, HWND, BOOL&) { ShowTab((m_cur + 1) % kTabCount); return 0; }
    LRESULT OnPanePrev(WORD, WORD, HWND, BOOL&) { ShowTab((m_cur + kTabCount - 1) % kTabCount); return 0; }

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    LRESULT OnUsage(WORD, WORD, HWND, BOOL&)
    {
        ::DialogBoxParamW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDD_USAGE),
            m_hWnd, &UsageDlgProc, 0);
        return 0;
    }

    LRESULT OnColors(WORD, WORD, HWND, BOOL&)
    {
        ::MessageBoxW(m_hWnd,
            L"The per-type colour legend is tied to the graphed Use Counts bars, "
            L"which are out of scope in this recreation.",
            L"RamMap", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LRESULT OnEmpty(WORD, WORD id, HWND, BOOL&)
    {
        bool ok = false;
        switch (id)
        {
        case ID_EMPTY_WORKINGSETS: ok = ram::EmptyMemoryList(ram::detail::MemoryEmptyWorkingSets); break;
        case ID_EMPTY_MODIFIED:    ok = ram::EmptyMemoryList(ram::detail::MemoryFlushModifiedList); break;
        case ID_EMPTY_STANDBY:     ok = ram::EmptyMemoryList(ram::detail::MemoryPurgeStandbyList); break;
        case ID_EMPTY_PRIORITY0:   ok = ram::EmptyMemoryList(ram::detail::MemoryPurgeLowPriorityStandbyList); break;
        case ID_EMPTY_SYSTEMWS:    ok = ram::EmptySystemWorkingSet(); break;
        default: break;
        }
        if (!ok)
        {
            ::MessageBoxW(m_hWnd, L"The empty operation failed. Administrative rights are required.",
                L"RamMap", MB_OK | MB_ICONWARNING);
        }
        Refresh();
        return 0;
    }

    LRESULT OnOpenSave(WORD, WORD, HWND, BOOL&)
    {
        ::MessageBoxW(m_hWnd,
            L"Saving and loading the native RamMap .rmp snapshot file is out of scope "
            L"in this recreation.",
            L"RamMap", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Find focuses the active tab's list. Report-mode list views do incremental
    // type-ahead search over the first column once focused.
    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        if (CListViewCtrl* lv = ActiveList())
        {
            lv->SetFocus();
        }
        return 0;
    }

    CListViewCtrl* ActiveList()
    {
        switch (m_cur)
        {
        case kUseCounts:   return &m_useCounts;
        case kProcesses:   return &m_processes;
        case kPriority:    return &m_priority;
        case kPhysRanges:  return &m_ranges;
        case kFileSummary: return &m_fileSummary;
        default:           return nullptr;
        }
    }

    // ---- config (registry) ---------------------------------------------
    static constexpr const wchar_t* kKey = L"Software\\Sysinternals\\RamMap";
    void LoadConfig()
    {
        HKEY k = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        {
            return;
        }
        WINDOWPLACEMENT wp{};
        DWORD cb = sizeof(wp);
        DWORD type = 0;
        if (::RegQueryValueExW(k, L"WindowPlacement", nullptr, &type, reinterpret_cast<LPBYTE>(&wp), &cb) == ERROR_SUCCESS
            && cb == sizeof(wp) && wp.length == sizeof(wp))
        {
            m_placement = wp;
            m_havePlacement = true;
        }
        ::RegCloseKey(k);
    }
    void SaveConfig()
    {
        HKEY k = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        {
            return;
        }
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(&wp))
        {
            ::RegSetValueExW(k, L"WindowPlacement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof(wp));
        }
        ::RegCloseKey(k);
    }

    void OnDestroy()
    {
        SaveConfig();
        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
        {
            pLoop->RemoveMessageFilter(this);
        }
        PostQuitMessage(0);
    }
};
