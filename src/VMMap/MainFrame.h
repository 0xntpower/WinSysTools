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
    ScopedWait() : prev(::SetCursor(::LoadCursorW(nullptr, IDC_WAIT))) {}
    ~ScopedWait() { ::SetCursor(prev); }
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
    std::vector<int> m_detailRows;   // indices into m_snap.regions after filtering
    bool m_showFree = false;

    int  m_splitY = 0;               // detail-pane top, measured from middle area
    bool m_dragging = false;
    static const int kSplitBar = 5;
    static const int kHeaderH = 92;
    static const int kFooterH = 28;

    HACCEL m_hAccel = nullptr;
    CFont  m_font;
    LOGFONTW m_logFont{};
    bool   m_haveFont = false;

    UINT m_findMsg = 0;
    CFindReplaceDialog* m_findDlg = nullptr;
    std::wstring m_lastFind;

    WINDOWPLACEMENT m_placement{};
    bool m_havePlacement = false;

    enum { kSumCols = 11, kDetCols = 13 };

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
        {
            return TRUE;
        }
        if (m_findDlg && m_findDlg->IsWindow() && m_findDlg->IsDialogMessageW(pMsg))
        {
            return TRUE;
        }
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

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
    int OnCreate(LPCREATESTRUCT)
    {
        m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        LoadConfig();

        CreateHeader();

        m_summary.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 0, IDC_SUMMARY);
        m_summary.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
        BuildSummaryColumns();

        m_detail.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, IDC_DETAIL);
        m_detail.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
        BuildDetailColumns();

        CreateFooter();

        if (HICON hi = static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(),
            L"APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
            m_hIcon.SetIcon(static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(),
                L"APPICON", IMAGE_ICON, 16, 16, 0)));
        }

        if (m_haveFont)
        {
            ApplyFont();
        }

        SetWindowTextW(L"VMMap - Sysinternals: www.sysinternals.com");

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
        {
            pLoop->AddMessageFilter(this);
        }

        SyncMenu();

        if (m_havePlacement)
        {
            SetWindowPlacement(&m_placement);
        }
        return 0;
    }

    void CreateHeader()
    {
        m_hIcon.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | SS_ICON, 0, IDC_APP_ICON);
        auto mk = [&](CStatic& s, UINT id, const wchar_t* text, DWORD extra = 0)
        {
            s.Create(m_hWnd, rcDefault, text, WS_CHILD | WS_VISIBLE | SS_LEFT | extra, 0, id);
            s.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
        };
        mk(m_procName, IDC_PROCESS_NAME, L"Process: (none)");
        mk(m_procPid, IDC_PROCESS_PID, L"PID:");
        mk(m_committed, IDC_COMMITTED_VAL, L"Committed:");
        mk(m_private, IDC_PRIVATE_VAL, L"Private Bytes:");
        mk(m_ws, IDC_WS_VAL, L"Working Set:");
    }

    void CreateFooter()
    {
        auto mk = [&](CButton& b, UINT id, const wchar_t* text)
        {
            b.Create(m_hWnd, rcDefault, text,
                WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON, 0, id);
            b.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
        };
        mk(m_btnTimeline, IDC_BTN_TIMELINE, L"Timeline...");
        mk(m_btnHeap, IDC_BTN_HEAPALLOC, L"Heap Allocations...");
        mk(m_btnCallTree, IDC_BTN_CALLTREE, L"Call Tree...");
        mk(m_btnTrace, IDC_BTN_TRACE, L"Trace...");
    }

    void BuildSummaryColumns()
    {
        struct { const wchar_t* t; int w; int fmt; } cols[kSumCols] = {
            { L"Type", 90, LVCFMT_LEFT },
            { L"Size", 80, LVCFMT_RIGHT },
            { L"Committed", 80, LVCFMT_RIGHT },
            { L"Private", 70, LVCFMT_RIGHT },
            { L"Total WS", 70, LVCFMT_RIGHT },
            { L"Private WS", 70, LVCFMT_RIGHT },
            { L"Shareable WS", 80, LVCFMT_RIGHT },
            { L"Shared WS", 70, LVCFMT_RIGHT },
            { L"Locked WS", 70, LVCFMT_RIGHT },
            { L"Blocks", 55, LVCFMT_RIGHT },
            { L"Largest", 80, LVCFMT_RIGHT },
        };
        for (int i = 0; i < kSumCols; ++i)
        {
            m_summary.InsertColumn(i, cols[i].t, cols[i].fmt, cols[i].w);
        }
    }

    void BuildDetailColumns()
    {
        struct { const wchar_t* t; int w; int fmt; } cols[kDetCols] = {
            { L"Address", 120, LVCFMT_LEFT },
            { L"Type", 90, LVCFMT_LEFT },
            { L"Size", 70, LVCFMT_RIGHT },
            { L"Committed", 70, LVCFMT_RIGHT },
            { L"Private", 65, LVCFMT_RIGHT },
            { L"Total WS", 65, LVCFMT_RIGHT },
            { L"Private WS", 65, LVCFMT_RIGHT },
            { L"Shareable WS", 75, LVCFMT_RIGHT },
            { L"Shared WS", 65, LVCFMT_RIGHT },
            { L"Locked WS", 65, LVCFMT_RIGHT },
            { L"Blocks", 50, LVCFMT_RIGHT },
            { L"Protection", 110, LVCFMT_LEFT },
            { L"Details", 300, LVCFMT_LEFT },
        };
        for (int i = 0; i < kDetCols; ++i)
        {
            m_detail.InsertColumn(i, cols[i].t, cols[i].fmt, cols[i].w);
        }
    }

    // ---- formatting -----------------------------------------------------
    static std::wstring FormatKb(uint64_t bytes)
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
    static std::wstring FormatCount(uint32_t n)
    {
        if (n == 0)
        {
            return std::wstring();
        }
        wchar_t raw[16] = {};
        swprintf(raw, 16, L"%lu", n);
        return Group(raw);
    }
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
    static std::wstring FormatAddr(uint64_t a)
    {
        wchar_t buf[24] = {};
        swprintf(buf, 24, L"%016llX", static_cast<unsigned long long>(a));
        return buf;
    }

    // ---- snapshot -------------------------------------------------------
    void TakeSnapshot(DWORD pid, const std::wstring& name)
    {
        ScopedWait wc;
        m_snap = vm::Capture(pid, name);
        if (!m_snap.ok)
        {
            ::MessageBoxW(m_hWnd, m_snap.error.c_str(), L"VMMap", MB_ICONWARNING);
            return;
        }
        PopulateSummary();
        RebuildDetail();
        UpdateHeader();
    }

    void UpdateHeader()
    {
        std::wstring n = L"Process: " + (m_snap.processName.empty() ? L"(none)" : m_snap.processName);
        m_procName.SetWindowTextW(n.c_str());
        wchar_t pid[32] = {};
        swprintf(pid, 32, L"PID: %lu", m_snap.pid);
        m_procPid.SetWindowTextW(pid);
        m_committed.SetWindowTextW((L"Committed: " + FormatKb(m_snap.committedTotal)).c_str());
        m_private.SetWindowTextW((L"Private Bytes: " + FormatKb(m_snap.privateTotal)).c_str());
        m_ws.SetWindowTextW((L"Working Set: " + FormatKb(m_snap.workingSetTotal)).c_str());
    }

    void PopulateSummary()
    {
        m_summary.SetRedraw(FALSE);
        m_summary.DeleteAllItems();
        int row = 0;
        for (int c = 0; c < vm::kCategoryCount; ++c)
        {
            const vm::CategoryTotal& t = m_snap.totals[c];
            m_summary.InsertItem(row, vm::CategoryName(static_cast<vm::Category>(c)));
            SetSum(row, 1, t.size);
            SetSum(row, 2, t.committed);
            SetSum(row, 3, t.privateBytes);
            SetSum(row, 4, t.totalWs);
            SetSum(row, 5, t.privateWs);
            SetSum(row, 6, t.shareableWs);
            SetSum(row, 7, t.sharedWs);
            SetSum(row, 8, t.lockedWs);
            m_summary.SetItemText(row, 9, FormatCount(t.blocks).c_str());
            SetSum(row, 10, t.largest);
            ++row;
        }
        m_summary.SetRedraw(TRUE);
    }

    void SetSum(int row, int col, uint64_t v)
    {
        m_summary.SetItemText(row, col, FormatKb(v).c_str());
    }

    void RebuildDetail()
    {
        m_detailRows.clear();
        for (int i = 0; i < static_cast<int>(m_snap.regions.size()); ++i)
        {
            const vm::Region& r = m_snap.regions[i];
            if (!m_showFree && r.category == vm::Category::Free)
            {
                continue;
            }
            m_detailRows.push_back(i);
        }
        m_detail.SetItemCount(static_cast<int>(m_detailRows.size()));
        m_detail.Invalidate();
    }

    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        if (p->idFrom != IDC_DETAIL)
        {
            return 0;
        }
        const int item = di->item.iItem;
        if (item < 0 || item >= static_cast<int>(m_detailRows.size()))
        {
            return 0;
        }
        const vm::Region& r = m_snap.regions[m_detailRows[item]];
        if (di->item.mask & LVIF_TEXT)
        {
            static std::wstring text;
            switch (di->item.iSubItem)
            {
            case 0:  text = FormatAddr(r.base); break;
            case 1:  text = vm::CategoryName(r.category); break;
            case 2:  text = FormatKb(r.size); break;
            case 3:  text = FormatKb(r.committed); break;
            case 4:  text = FormatKb(r.privateBytes); break;
            case 5:  text = FormatKb(r.totalWs); break;
            case 6:  text = FormatKb(r.privateWs); break;
            case 7:  text = FormatKb(r.shareableWs); break;
            case 8:  text = FormatKb(r.sharedWs); break;
            case 9:  text = FormatKb(r.lockedWs); break;
            case 10: text = L"1"; break;
            case 11: text = vm::ProtectionText(r); break;
            case 12: text = r.details; break;
            default: text.clear(); break;
            }
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
        return 0;
    }

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT, CSize)
    {
        CRect rc;
        GetClientRect(&rc);
        LayoutHeader(rc);

        const int top = kHeaderH;
        const int bottom = rc.bottom - kFooterH;
        int usable = bottom - top;
        if (usable < 60)
        {
            return;
        }
        if (m_splitY < 40 || m_splitY > usable - 40)
        {
            m_splitY = usable * 2 / 5;
        }
        m_summary.SetWindowPos(nullptr, 0, top, rc.right, m_splitY, SWP_NOZORDER);
        const int detTop = top + m_splitY + kSplitBar;
        m_detail.SetWindowPos(nullptr, 0, detTop, rc.right, bottom - detTop, SWP_NOZORDER);

        LayoutFooter(rc);
    }

    void LayoutHeader(const CRect& rc)
    {
        m_hIcon.SetWindowPos(nullptr, 6, 6, 20, 20, SWP_NOZORDER);
        m_procName.SetWindowPos(nullptr, 34, 6, rc.right - 40, 14, SWP_NOZORDER);
        m_procPid.SetWindowPos(nullptr, 34, 22, rc.right - 40, 14, SWP_NOZORDER);
        m_committed.SetWindowPos(nullptr, 8, 42, 300, 14, SWP_NOZORDER);
        m_private.SetWindowPos(nullptr, 8, 58, 300, 14, SWP_NOZORDER);
        m_ws.SetWindowPos(nullptr, 8, 74, 300, 14, SWP_NOZORDER);
    }

    void LayoutFooter(const CRect& rc)
    {
        const int y = rc.bottom - kFooterH + 7;
        const int w = 76;
        int x = rc.right - (w + 4) * 4 - 4;
        m_btnTimeline.SetWindowPos(nullptr, x, y, w, 20, SWP_NOZORDER); x += w + 4;
        m_btnHeap.SetWindowPos(nullptr, x, y, w, 20, SWP_NOZORDER); x += w + 4;
        m_btnCallTree.SetWindowPos(nullptr, x, y, w, 20, SWP_NOZORDER); x += w + 4;
        m_btnTrace.SetWindowPos(nullptr, x, y, w, 20, SWP_NOZORDER);
    }

    CRect SplitterRect()
    {
        CRect rc;
        GetClientRect(&rc);
        const int top = kHeaderH;
        return CRect(0, top + m_splitY, rc.right, top + m_splitY + kSplitBar);
    }

    BOOL OnSetCursor(CWindow wnd, UINT hit, UINT)
    {
        if (wnd.m_hWnd == m_hWnd && hit == HTCLIENT)
        {
            POINT pt{};
            ::GetCursorPos(&pt);
            ScreenToClient(&pt);
            if (SplitterRect().PtInRect(pt))
            {
                ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
        }
        SetMsgHandled(FALSE);
        return FALSE;
    }
    void OnLButtonDown(UINT, CPoint pt)
    {
        if (SplitterRect().PtInRect(pt)) { m_dragging = true; SetCapture(); }
        else SetMsgHandled(FALSE);
    }
    void OnMouseMove(UINT, CPoint pt)
    {
        if (!m_dragging) { SetMsgHandled(FALSE); return; }
        m_splitY = pt.y - kHeaderH;
        OnSize(0, CSize());
    }
    void OnLButtonUp(UINT, CPoint)
    {
        if (m_dragging) { m_dragging = false; ReleaseCapture(); }
        else SetMsgHandled(FALSE);
    }

    // ---- commands -------------------------------------------------------
    LRESULT OnSelectProcess(WORD, WORD, HWND, BOOL&)
    {
        CSelectProcessDlg dlg;
        if (dlg.DoModal(m_hWnd) == IDOK && dlg.m_pid != 0)
        {
            TakeSnapshot(dlg.m_pid, dlg.m_name);
        }
        return 0;
    }
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&)
    {
        if (m_snap.pid != 0)
        {
            TakeSnapshot(m_snap.pid, m_snap.processName);
        }
        return 0;
    }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    LRESULT OnShowFree(WORD, WORD, HWND, BOOL&)
    {
        m_showFree = !m_showFree;
        SyncMenu();
        RebuildDetail();
        return 0;
    }

    LRESULT OnEmptyWs(WORD, WORD, HWND, BOOL&)
    {
        if (m_snap.pid == 0)
        {
            return 0;
        }
        vm::Handle proc(::OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, m_snap.pid));
        if (proc.valid())
        {
            ::SetProcessWorkingSetSize(proc.get(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
            TakeSnapshot(m_snap.pid, m_snap.processName);
        }
        return 0;
    }

    LRESULT OnFont(WORD, WORD, HWND, BOOL&)
    {
        LOGFONTW lf = m_logFont;
        if (!m_haveFont)
        {
            ::GetObjectW(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)), sizeof(lf), &lf);
        }
        CHOOSEFONTW cf{};
        cf.lStructSize = sizeof(cf);
        cf.hwndOwner = m_hWnd;
        cf.lpLogFont = &lf;
        cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
        if (::ChooseFontW(&cf))
        {
            m_logFont = lf;
            m_haveFont = true;
            ApplyFont();
        }
        return 0;
    }
    void ApplyFont()
    {
        m_font = ::CreateFontIndirectW(&m_logFont);
        m_summary.SetFont(m_font);
        m_detail.SetFont(m_font);
    }

    // Copy the address of the focused detail row.
    LRESULT OnCopyAddr(WORD, WORD, HWND, BOOL&)
    {
        int i = m_detail.GetNextItem(-1, LVNI_FOCUSED);
        if (i >= 0 && i < static_cast<int>(m_detailRows.size()))
        {
            CopyText(FormatAddr(m_snap.regions[m_detailRows[i]].base));
        }
        return 0;
    }

    // Copy the whole detail list as tab-separated text.
    LRESULT OnCopyAll(WORD, WORD, HWND, BOOL&)
    {
        std::wstring out = BuildExport(L'\t');
        CopyText(out);
        return 0;
    }

    LRESULT OnSaveAs(WORD, WORD, HWND, BOOL&)
    {
        if (m_snap.pid == 0)
        {
            return 0;
        }
        wchar_t path[MAX_PATH] = L"vmmap.csv";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hWnd;
        ofn.lpstrFilter = L"CSV files\0*.csv\0All files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"csv";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (::GetSaveFileNameW(&ofn) && !vm::WriteCsv(m_snap, path, m_showFree))
        {
            ::MessageBoxW(m_hWnd, L"Unable to write the output file.", L"VMMap", MB_ICONWARNING);
        }
        return 0;
    }

    std::wstring BuildExport(wchar_t sep)
    {
        std::wstring out;
        const wchar_t* heads[kDetCols] = {
            L"Address", L"Type", L"Size", L"Committed", L"Private", L"Total WS",
            L"Private WS", L"Shareable WS", L"Shared WS", L"Locked WS", L"Blocks",
            L"Protection", L"Details" };
        for (int c = 0; c < kDetCols; ++c)
        {
            if (c) out.push_back(sep);
            out += heads[c];
        }
        out += L"\r\n";
        for (int idx : m_detailRows)
        {
            const vm::Region& r = m_snap.regions[idx];
            std::wstring cells[kDetCols] = {
                FormatAddr(r.base), vm::CategoryName(r.category), FormatKb(r.size),
                FormatKb(r.committed), FormatKb(r.privateBytes), FormatKb(r.totalWs),
                FormatKb(r.privateWs), FormatKb(r.shareableWs), FormatKb(r.sharedWs),
                FormatKb(r.lockedWs), L"1", vm::ProtectionText(r), r.details };
            for (int c = 0; c < kDetCols; ++c)
            {
                if (c) out.push_back(sep);
                out += cells[c];
            }
            out += L"\r\n";
        }
        return out;
    }

    void CopyText(const std::wstring& text)
    {
        if (!::OpenClipboard(m_hWnd))
        {
            return;
        }
        ::EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void* dst = ::GlobalLock(h))
            {
                memcpy(dst, text.c_str(), bytes);
                ::GlobalUnlock(h);
                ::SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        ::CloseClipboard();
    }

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        if (m_findDlg && m_findDlg->IsWindow())
        {
            m_findDlg->SetActiveWindow();
            return 0;
        }
        m_findDlg = new CFindReplaceDialog();
        m_findDlg->Create(TRUE, m_lastFind.c_str(), nullptr, FR_DOWN, m_hWnd);
        return 0;
    }
    LRESULT OnFindNext(WORD, WORD, HWND, BOOL&)
    {
        if (!m_lastFind.empty())
        {
            DoFind(m_lastFind);
        }
        return 0;
    }
    LRESULT OnFindMsg(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
        if (fr->Flags & FR_DIALOGTERM)
        {
            m_findDlg = nullptr;
            return 0;
        }
        if (fr->Flags & FR_FINDNEXT)
        {
            m_lastFind = fr->lpstrFindWhat ? fr->lpstrFindWhat : L"";
            DoFind(m_lastFind);
        }
        return 0;
    }
    void DoFind(const std::wstring& needle)
    {
        if (needle.empty() || m_detailRows.empty())
        {
            return;
        }
        std::wstring low = needle;
        std::transform(low.begin(), low.end(), low.begin(), towlower);
        int start = m_detail.GetNextItem(-1, LVNI_FOCUSED);
        const int n = static_cast<int>(m_detailRows.size());
        for (int step = 1; step <= n; ++step)
        {
            int i = (start + step) % n;
            const vm::Region& r = m_snap.regions[m_detailRows[i]];
            std::wstring hay = FormatAddr(r.base) + L" " + vm::CategoryName(r.category)
                             + L" " + vm::ProtectionText(r) + L" " + r.details;
            std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
            if (hay.find(low) != std::wstring::npos)
            {
                m_detail.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                m_detail.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                m_detail.EnsureVisible(i, FALSE);
                return;
            }
        }
    }

    LRESULT OnQuickHelp(WORD, WORD, HWND, BOOL&)
    {
        ::MessageBoxW(m_hWnd,
            L"VMMap shows process virtual and physical memory (working set) usage, "
            L"categorized by type: Image, Mapped File, Shareable, Heap, Managed Heap, "
            L"Stack, Private Data, Page Table, Unusable and Free.\r\n\r\n"
            L"For each type it reports Size, Committed, Private, Total WS, Private WS, "
            L"Shareable WS, Shared WS, Locked WS, Blocks and Largest.",
            L"VMMap Quick Help", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    LRESULT OnCmdLine(WORD, WORD, HWND, BOOL&)
    {
        ::DialogBoxParamW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDD_USAGE),
            m_hWnd, &UsageProc, 0);
        return 0;
    }
    LRESULT OnHelp(WORD, WORD, HWND, BOOL&)
    {
        ::ShellExecuteW(m_hWnd, L"open", L"https://learn.microsoft.com/sysinternals/downloads/vmmap",
            nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    static INT_PTR CALLBACK UsageProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM)
    {
        if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
        {
            ::EndDialog(dlg, LOWORD(wParam));
            return TRUE;
        }
        return msg == WM_INITDIALOG ? TRUE : FALSE;
    }

    void SyncMenu()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull())
        {
            return;
        }
        menu.CheckMenuItem(ID_OPT_SHOWFREE, MF_BYCOMMAND | (m_showFree ? MF_CHECKED : MF_UNCHECKED));
    }

    // ---- config (registry) ---------------------------------------------
    static constexpr const wchar_t* kKey = L"Software\\Sysinternals\\VMMap";
    void LoadConfig()
    {
        HKEY k = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        {
            return;
        }
        DWORD dw = 0, cb = sizeof(dw), type = 0;
        if (::RegQueryValueExW(k, L"ShowFreeRegions", nullptr, &type, reinterpret_cast<LPBYTE>(&dw), &cb) == ERROR_SUCCESS)
        {
            m_showFree = dw != 0;
        }
        WINDOWPLACEMENT wp{};
        cb = sizeof(wp);
        if (::RegQueryValueExW(k, L"WindowPlacement", nullptr, &type, reinterpret_cast<LPBYTE>(&wp), &cb) == ERROR_SUCCESS
            && cb == sizeof(wp) && wp.length == sizeof(wp))
        {
            m_placement = wp;
            m_havePlacement = true;
        }
        if (::RegQueryValueExW(k, L"Font", nullptr, &type, reinterpret_cast<LPBYTE>(&m_logFont), (cb = sizeof(m_logFont), &cb)) == ERROR_SUCCESS
            && cb == sizeof(m_logFont) && m_logFont.lfFaceName[0])
        {
            m_haveFont = true;
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
        DWORD dw = m_showFree ? 1 : 0;
        ::RegSetValueExW(k, L"ShowFreeRegions", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(&wp))
        {
            ::RegSetValueExW(k, L"WindowPlacement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof(wp));
        }
        if (m_haveFont)
        {
            ::RegSetValueExW(k, L"Font", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&m_logFont), sizeof(m_logFont));
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
