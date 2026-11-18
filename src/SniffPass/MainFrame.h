// MainFrame.h -- WTL main window for the SniffPass recreation. The original registers a
// plain "SniffPass" window class and hangs a single owner-data SysListView32, a toolbar
// and a status bar off it. This rebuilds that layout over the record list. Start Capture
// routes through the Capture backend, which performs no live capture in this recreation:
// it probes the selected driver and reports when the driver is missing, matching the
// original's failure path, without ever reading a packet.
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
#include "Capture.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"SniffPass", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    Config m_cfg;
    bool m_noSaveLoad = false;

    std::vector<PasswordRecord> m_records;

    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg)) return TRUE;
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        if (uMsg == m_uFindMsg) { OnFindReplaceMsg(lParam); lResult = 0; return TRUE; }
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnGetDispInfo)
        NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
        NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
        NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClk)
        NOTIFY_CODE_HANDLER(NM_RCLICK, OnRClick)
        COMMAND_ID_HANDLER(ID_SP_STARTCAP, OnStartCapture)
        COMMAND_ID_HANDLER(ID_SP_STOPCAP, OnStopCapture)
        COMMAND_ID_HANDLER(ID_SP_CLEAR, OnClear)
        COMMAND_ID_HANDLER(ID_SP_IMPORT, OnImport)
        COMMAND_ID_HANDLER(ID_SP_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_SP_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_SP_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_SP_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_SP_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_SP_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_SP_CHOOSECOLS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_SP_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_SP_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_SP_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_SP_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_SP_EXCLUDEDUP, OnExcludeDup)
        COMMAND_ID_HANDLER(ID_SP_BEEP, OnBeep)
        COMMAND_ID_HANDLER(ID_SP_ADDHEADER, OnAddHeader)
        COMMAND_ID_HANDLER(ID_SP_CAPOPTIONS, OnCaptureOptions)
        COMMAND_ID_HANDLER(ID_SP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_SP_EXIT, OnExit)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()
    LRESULT OnEraseBkgnd(CDCHandle dc) { return FillFrameBtnFace(m_hWnd, dc); }

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        if (!m_noSaveLoad) m_cfg.load();
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        CreateToolbar();
        m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);

        m_list.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, IDC_LIST);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

        SetWindowTextW(L"SniffPass");
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        RebuildColumns();
        ApplyGridLines();
        SyncChecks();
        if (m_cfg.havePlacement) SetWindowPlacement(&m_cfg.placement);

        if (CMessageLoop* pLoop = _Module.GetMessageLoop()) pLoop->AddMessageFilter(this);
        UpdateStatus();
        return 0;
    }

    void CreateToolbar()
    {
        CToolBarCtrl tb;
        tb.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0, IDC_TOOLBAR);
        tb.SetButtonStructSize(sizeof(TBBUTTON));
        PinToolbarLight(tb);
        tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
        // Toolbar strip 104 image order: 0 Find, 1 Save, 2 Copy, 3 Properties, 4 Exit,
        // 5 Start Capture, 6 Stop Capture. Button order matches the original.
        struct { int bmp, cmd; } btns[] = {
            { 5, ID_SP_STARTCAP }, { 6, ID_SP_STOPCAP },
            { -1, 0 },
            { 1, ID_SP_SAVE }, { 2, ID_SP_COPY }, { 3, ID_SP_PROPERTIES }, { 0, ID_SP_FIND },
            { -1, 0 },
            { 4, ID_SP_EXIT },
        };
        std::vector<TBBUTTON> b;
        for (auto& x : btns)
        {
            TBBUTTON t{};
            if (x.bmp < 0) { t.fsStyle = TBSTYLE_SEP; }
            else { t.iBitmap = x.bmp; t.idCommand = x.cmd; t.fsState = TBSTATE_ENABLED; t.fsStyle = TBSTYLE_BUTTON; }
            b.push_back(t);
        }
        tb.AddButtons((int)b.size(), b.data());
        tb.AutoSize();
        m_hWndToolBar = tb;
    }

    // ---- layout ---------------------------------------------------------
    int toolbarHeight() { CRect rc; if (m_hWndToolBar) ::GetWindowRect(m_hWndToolBar, &rc); return m_hWndToolBar ? rc.Height() : 0; }
    int statusHeight()  { CRect rc; if (m_hWndStatusBar) ::GetWindowRect(m_hWndStatusBar, &rc); return m_hWndStatusBar ? rc.Height() : 0; }

    void OnSize(UINT, CSize)
    {
        if (m_hWndToolBar) ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
        if (m_hWndStatusBar) ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);
        CRect rc; GetClientRect(&rc);
        int top = toolbarHeight();
        int bottom = rc.bottom - statusHeight();
        m_list.SetWindowPos(nullptr, 0, top, rc.right, bottom - top, SWP_NOZORDER);
    }

    // ---- columns --------------------------------------------------------
    void RebuildColumns()
    {
        while (m_list.DeleteColumn(0)) {}
        int i = 0;
        for (size_t k = 0; k < m_cfg.cols.visible.size(); ++k)
        {
            int c = m_cfg.cols.visible[k];
            int w = k < m_cfg.cols.widths.size() ? m_cfg.cols.widths[k] : kColumns[c].width;
            m_list.InsertColumn(i++, kColumns[c].title, kColumns[c].format, w);
        }
        m_list.SetItemCount((int)m_records.size());
    }

    void ApplyGridLines()
    {
        DWORD ex = m_list.GetExtendedListViewStyle();
        ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
        m_list.SetExtendedListViewStyle(ex);
    }

    // ---- sorting / data -------------------------------------------------
    void SortRecords()
    {
        int col = m_cfg.cols.sortColumn;
        bool asc = m_cfg.cols.sortAscending;
        std::stable_sort(m_records.begin(), m_records.end(), [&](const PasswordRecord& a, const PasswordRecord& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
            int c;
            if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    // ---- owner-data -----------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        static std::wstring text;
        if (!(di->item.mask & LVIF_TEXT)) return 0;
        int item = di->item.iItem;
        if (item < 0 || item >= (int)m_records.size()) return 0;
        if (di->item.iSubItem < 0 || di->item.iSubItem >= (int)m_cfg.cols.visible.size()) return 0;
        int col = m_cfg.cols.visible[di->item.iSubItem];
        text = m_records[item].column(col);
        di->item.pszText = const_cast<LPWSTR>(text.c_str());
        return 0;
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        auto* nv = reinterpret_cast<LPNMLISTVIEW>(p);
        int disp = nv->iSubItem;
        if (disp < 0 || disp >= (int)m_cfg.cols.visible.size()) return 0;
        int clicked = m_cfg.cols.visible[disp];
        if (clicked == m_cfg.cols.sortColumn) m_cfg.cols.sortAscending = !m_cfg.cols.sortAscending;
        else { m_cfg.cols.sortColumn = clicked; m_cfg.cols.sortAscending = true; }
        SortRecords();
        m_list.Invalidate();
        return 0;
    }

    LRESULT OnItemChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }
    LRESULT OnDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnProperties(0, 0, nullptr, b); return 0; }

    LRESULT OnRClick(int, LPNMHDR, BOOL&)
    {
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CTX_MENU));
        if (!menu) return 0;
        HMENU sub = ::GetSubMenu(menu, 0);
        POINT pt; ::GetCursorPos(&pt);
        ::TrackPopupMenu(sub, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
        ::DestroyMenu(menu);
        return 0;
    }

    // ---- status ---------------------------------------------------------
    void UpdateStatus()
    {
        wchar_t buf[128];
        int sel = m_list.GetSelectedCount();
        if (sel > 0) swprintf(buf, 128, L"%d item(s), %d Selected", (int)m_records.size(), sel);
        else         swprintf(buf, 128, L"%d item(s)", (int)m_records.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
    }

    // ---- selection helpers ----------------------------------------------
    std::vector<int> selectedRows()
    {
        std::vector<int> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            v.push_back(i);
        return v;
    }

    ExportRequest buildRequest()
    {
        ExportRequest r;
        r.colDefs = kColumns;
        r.columns = m_cfg.cols.visible;
        r.reportTitle = L"Sniffed Passwords";
        r.addHeaderLine = m_cfg.addExportHeaderLine;
        r.rowCount = (int)m_records.size();
        r.cell = [this](int row, int col) { return m_records[row].column(col); };
        return r;
    }

    // ---- capture --------------------------------------------------------
    LRESULT OnStartCapture(WORD, WORD, HWND, BOOL&)
    {
        int method = m_cfg.captureMethod;
        if (!IsBackendAvailable(method))
        {
            // Matches STRINGTABLE 9 in the original, shown when the selected driver is
            // not present on the machine.
            MessageBoxW(L"Failed to start capturing packets from the current network adapter.",
                        L"SniffPass", MB_OK | MB_ICONERROR);
            return 0;
        }
        // The backend is present, but this recreation never reads live traffic. Say so
        // plainly rather than open a capture that could see real credentials.
        std::wstring msg = L"This recreation does not perform live packet capture.\n\nThe ";
        msg += CaptureMethodName(method);
        msg += L" backend was detected, but capture is disabled by design.";
        MessageBoxW(msg.c_str(), L"SniffPass", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    LRESULT OnStopCapture(WORD, WORD, HWND, BOOL&) { return 0; }

    LRESULT OnClear(WORD, WORD, HWND, BOOL&)
    {
        m_records.clear();
        m_list.SetItemCount(0);
        m_list.Invalidate();
        UpdateStatus();
        return 0;
    }

    LRESULT OnImport(WORD, WORD, HWND, BOOL&)
    {
        // Importing a tcpdump/libpcap file would run the parser over whatever a user
        // supplied file holds, which could be real traffic. This recreation keeps to
        // synthetic data only, so import is not offered here.
        MessageBoxW(L"Importing from a capture file is not available in this recreation.",
                    L"SniffPass", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // ---- commands -------------------------------------------------------
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }
    LRESULT OnCaptureOptions(WORD, WORD, HWND, BOOL&) { CCaptureDlg(m_cfg).DoModal(m_hWnd); return 0; }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        CFileDialog dlg(FALSE, L"txt", L"Passwords.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
            L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.html)\0*.html\0"
            L"HTML File - Vertical (*.html)\0*.html\0XML File (*.xml)\0*.xml\0"
            L"Comma delimited text file (*.csv)\0*.csv\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex > 0 ? m_cfg.saveFilterIndex : 1;
        if (dlg.DoModal() != IDOK) return 0;
        m_cfg.saveFilterIndex = dlg.m_ofn.nFilterIndex;
        ExportFormat fmt = FormatFromFilterIndex(dlg.m_ofn.nFilterIndex);

        auto rows = selectedRows();
        ExportRequest r = buildRequest();
        if (!rows.empty())
        {
            ExportRequest base = buildRequest();
            r.rowCount = (int)rows.size();
            r.cell = [base, rows](int row, int col) { return base.cell(rows[row], col); };
        }
        WriteReport(dlg.m_szFileName, r, fmt);
        return 0;
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        auto rows = selectedRows();
        if (rows.empty()) return 0;
        ExportRequest base = buildRequest();
        ExportRequest r = base;
        r.addHeaderLine = false;
        r.rowCount = (int)rows.size();
        r.cell = [&](int row, int col) { return base.cell(rows[row], col); };
        CopyToClipboard(BuildReport(r, ExportFormat::Tab));
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_records.size()) return 0;
        std::vector<std::pair<std::wstring, std::wstring>> rows;
        for (int c : m_cfg.cols.visible) rows.emplace_back(kColumns[c].title, m_records[i].column(c));
        CPropertiesDlg(std::move(rows)).DoModal(m_hWnd);
        return 0;
    }

    void ShowHtml(bool selectedOnly)
    {
        ExportRequest r = buildRequest();
        if (selectedOnly)
        {
            auto rows = selectedRows();
            if (!rows.empty())
            {
                ExportRequest base = buildRequest();
                r.rowCount = (int)rows.size();
                r.cell = [base, rows](int row, int col) { return base.cell(rows[row], col); };
            }
        }
        wchar_t dir[MAX_PATH], tmp[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"spw", 0, tmp);
        std::wstring html = tmp; html += L".html";
        WriteReport(html, r, ExportFormat::HtmlHorizontal);
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(false); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&) { ShowHtml(true); return 0; }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        CColumnsDlg dlg(kColumns, kColumnCount, m_cfg.cols);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            m_cfg.cols = dlg.m_state;
            if (std::find(m_cfg.cols.visible.begin(), m_cfg.cols.visible.end(), m_cfg.cols.sortColumn) == m_cfg.cols.visible.end()
                && !m_cfg.cols.visible.empty())
                m_cfg.cols.sortColumn = m_cfg.cols.visible.front();
            RebuildColumns();
            m_list.Invalidate();
        }
        return 0;
    }

    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&)
    {
        for (int i = 0; i < (int)m_cfg.cols.visible.size(); ++i) m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
        for (size_t i = 0; i < m_cfg.cols.visible.size(); ++i)
            if (i < m_cfg.cols.widths.size()) m_cfg.cols.widths[i] = m_list.GetColumnWidth((int)i);
        return 0;
    }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED); UpdateStatus(); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, 0, LVIS_SELECTED); UpdateStatus(); return 0; }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_cfg.showGridLines = !m_cfg.showGridLines; ApplyGridLines(); SyncChecks(); return 0; }
    LRESULT OnExcludeDup(WORD, WORD, HWND, BOOL&) { m_cfg.excludeDuplicates = !m_cfg.excludeDuplicates; SyncChecks(); return 0; }
    LRESULT OnBeep(WORD, WORD, HWND, BOOL&) { m_cfg.beepOnNewPassword = !m_cfg.beepOnNewPassword; SyncChecks(); return 0; }
    LRESULT OnAddHeader(WORD, WORD, HWND, BOOL&) { m_cfg.addExportHeaderLine = !m_cfg.addExportHeaderLine; SyncChecks(); return 0; }

    void SyncChecks()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull()) return;
        auto chk = [&](UINT id, bool on) { menu.CheckMenuItem(id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
        chk(ID_SP_GRIDLINES, m_cfg.showGridLines);
        chk(ID_SP_EXCLUDEDUP, m_cfg.excludeDuplicates);
        chk(ID_SP_BEEP, m_cfg.beepOnNewPassword);
        chk(ID_SP_ADDHEADER, m_cfg.addExportHeaderLine);
    }

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        if (m_pFind) { m_pFind->SetFocus(); return 0; }
        m_pFind = new CFindReplaceDialog();
        m_pFind->Create(TRUE, L"", nullptr, FR_DOWN, m_hWnd);
        return 0;
    }
    void OnFindReplaceMsg(LPARAM lParam)
    {
        auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
        if (fr->Flags & FR_DIALOGTERM) { m_pFind = nullptr; return; }
        if (fr->Flags & FR_FINDNEXT) FindText(fr->lpstrFindWhat, (fr->Flags & FR_DOWN) != 0);
    }
    void FindText(const std::wstring& what, bool down)
    {
        int n = (int)m_records.size();
        if (what.empty() || n == 0) return;
        std::wstring needle = what;
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
        for (int step = 1; step <= n; ++step)
        {
            int i = down ? (start + step) % n : ((start - step) % n + n) % n;
            for (int c : m_cfg.cols.visible)
            {
                std::wstring v = m_records[i].column(c);
                std::transform(v.begin(), v.end(), v.begin(), towlower);
                if (v.find(needle) != std::wstring::npos)
                {
                    m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    m_list.EnsureVisible(i, FALSE);
                    return;
                }
            }
        }
    }

    void CopyToClipboard(const std::string& ansi)
    {
        std::wstring text = Widen(ansi);
        if (!::OpenClipboard(m_hWnd)) return;
        ::EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(::GlobalLock(h), text.c_str(), bytes);
            ::GlobalUnlock(h);
            ::SetClipboardData(CF_UNICODETEXT, h);
        }
        ::CloseClipboard();
    }

    void OnDestroy()
    {
        if (!m_noSaveLoad)
        {
            for (size_t i = 0; i < m_cfg.cols.visible.size(); ++i)
                if (i < m_cfg.cols.widths.size()) m_cfg.cols.widths[i] = m_list.GetColumnWidth((int)i);
            m_cfg.placement.length = sizeof(m_cfg.placement);
            if (GetWindowPlacement(&m_cfg.placement)) m_cfg.havePlacement = true;
            m_cfg.save();
        }
        PostQuitMessage(0);
    }
};
