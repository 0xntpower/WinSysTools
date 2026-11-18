// MainFrame.h -- WTL main window for the PreviousFilesRecovery recreation. The layout
// matches the original (menu 102, flat toolbar from bitmap 110, the embedded search
// panel dialog 111 docked under the toolbar, an owner-data report ListView for the
// results, and a status bar). Logic runs over the clean ScanPreviousFiles model
// instead of the decompiled offset code.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <uxtheme.h>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#pragma comment(lib, "uxtheme.lib")
#include "resource.h"
#include "../common/UiUtil.h"
#include "Columns.h"
#include "Config.h"
#include "PrevFileRecord.h"
#include "Scanner.h"
#include "ShadowCopy.h"
#include "Export.h"
#include "Dialogs.h"

extern CAppModule _Module;

// ---- embedded search-options panel (dialog 111) --------------------------
class CSearchPanel : public CDialogImpl<CSearchPanel>
{
public:
    enum { IDD = IDD_SEARCH };

    std::function<void()> onStart;

    BEGIN_MSG_MAP(CSearchPanel)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDC_SRCH_BROWSE, OnBrowse)
        COMMAND_ID_HANDLER(IDC_SRCH_START, OnStart)
        COMMAND_HANDLER(IDC_SRCH_MODE, CBN_SELCHANGE, OnModeChange)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        CComboBox depth = GetDlgItem(IDC_SRCH_DEPTH);
        depth.AddString(L"Unlimited");
        for (int i = 1; i < 20; ++i) depth.AddString(std::to_wstring(i).c_str());
        depth.SetCurSel(0);

        CComboBox mode = GetDlgItem(IDC_SRCH_MODE);
        mode.AddString(L"Find all file copies");
        mode.AddString(L"Find all file copies with different size/write time");
        mode.AddString(L"Find only the latest file version");
        mode.AddString(L"Find only files in the selected shadow copy");
        mode.SetCurSel(1);

        FillShadows();
        return TRUE;
    }

    void FillShadows()
    {
        CComboBox shadow = GetDlgItem(IDC_SRCH_SHADOW);
        shadow.ResetContent();
        for (const auto& s : EnumerateShadowCopies()) shadow.AddString(s.deviceObject.c_str());
        if (shadow.GetCount() > 0) shadow.SetCurSel(0);
        shadow.EnableWindow(GetMode() == 4);
    }

    void ApplyConfig(const Config& c)
    {
        SetDlgItemTextW(IDC_SRCH_BASEFOLDER, c.baseFolder.c_str());
        SetDlgItemTextW(IDC_SRCH_WILDCARD, c.filesWildcard.c_str());
        CComboBox(GetDlgItem(IDC_SRCH_DEPTH)).SetCurSel(std::clamp(c.subfoldersDepth, 0, 19));
        CComboBox(GetDlgItem(IDC_SRCH_MODE)).SetCurSel(std::clamp(c.multipleCopiesMode - 1, 0, 3));
        CheckDlgButton(IDC_SRCH_FINDPREV, c.findPreviousFiles ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SRCH_FINDDELFILE, c.findDeletedFiles ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SRCH_FINDDELDIR, c.findDeletedFolders ? BST_CHECKED : BST_UNCHECKED);
        CWindow(GetDlgItem(IDC_SRCH_SHADOW)).EnableWindow(GetMode() == 4);
    }

    void ReadConfig(Config& c) const
    {
        wchar_t buf[MAX_PATH]{};
        const_cast<CSearchPanel*>(this)->GetDlgItemTextW(IDC_SRCH_BASEFOLDER, buf, MAX_PATH);
        c.baseFolder = buf;
        wchar_t wc[128]{};
        const_cast<CSearchPanel*>(this)->GetDlgItemTextW(IDC_SRCH_WILDCARD, wc, 128);
        c.filesWildcard = wc[0] ? std::wstring(wc) : std::wstring(L"*");
        c.subfoldersDepth = CComboBox(const_cast<CSearchPanel*>(this)->GetDlgItem(IDC_SRCH_DEPTH)).GetCurSel();
        c.multipleCopiesMode = GetMode();
        c.findPreviousFiles = const_cast<CSearchPanel*>(this)->IsDlgButtonChecked(IDC_SRCH_FINDPREV) == BST_CHECKED;
        c.findDeletedFiles = const_cast<CSearchPanel*>(this)->IsDlgButtonChecked(IDC_SRCH_FINDDELFILE) == BST_CHECKED;
        c.findDeletedFolders = const_cast<CSearchPanel*>(this)->IsDlgButtonChecked(IDC_SRCH_FINDDELDIR) == BST_CHECKED;
    }

    int GetMode() const { return CComboBox(const_cast<CSearchPanel*>(this)->GetDlgItem(IDC_SRCH_MODE)).GetCurSel() + 1; }

    std::wstring SelectedShadow()
    {
        CComboBox shadow = GetDlgItem(IDC_SRCH_SHADOW);
        wchar_t buf[600]{};
        shadow.GetWindowTextW(buf, 600);
        return buf;
    }

    void SetStatus(const std::wstring& s) { SetDlgItemTextW(IDC_SRCH_STATUS, s.c_str()); }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        CFolderDialog dlg(m_hWnd, L"Choose the base folder to scan");
        if (dlg.DoModal(m_hWnd) == IDOK) SetDlgItemTextW(IDC_SRCH_BASEFOLDER, dlg.GetFolderPath());
        return 0;
    }
    LRESULT OnStart(WORD, WORD, HWND, BOOL&) { if (onStart) onStart(); return 0; }
    LRESULT OnModeChange(WORD, WORD, HWND, BOOL&)
    { CWindow(GetDlgItem(IDC_SRCH_SHADOW)).EnableWindow(GetMode() == 4); return 0; }
};

// ---- main window ---------------------------------------------------------
class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"PreviousFilesRecovery", IDR_MAIN_MENU)

    CSearchPanel m_panel;
    CListViewCtrl m_list;
    std::vector<PrevFileRecord> m_items;
    Config m_cfg;
    bool m_noSaveLoad = false;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    int m_rclickRow = -1, m_rclickCol = -1;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg)) return TRUE;
        if (m_panel.m_hWnd && ::IsDialogMessageW(m_panel.m_hWnd, pMsg)) return TRUE;
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        if (uMsg == m_uFindMsg) { OnFindReplaceMsg(lParam); lResult = 0; return TRUE; }
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ODSTATECHANGED, OnStateChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
        COMMAND_ID_HANDLER(ID_PFR_STARTSCAN, OnStartScan)
        COMMAND_ID_HANDLER(ID_PFR_REFRESH, OnStartScan)
        COMMAND_ID_HANDLER(ID_PFR_RECOVER, OnRecover)
        COMMAND_ID_HANDLER(ID_PFR_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_PFR_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_PFR_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_PFR_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_PFR_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_PFR_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_PFR_TOOLTIPS, OnTooltips)
        COMMAND_ID_HANDLER(ID_PFR_MARKODDEVEN, OnMarkOddEven)
        COMMAND_ID_HANDLER(ID_PFR_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_PFR_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_PFR_CHOOSECOLS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_PFR_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_PFR_AUTOSIZE_HDR, OnAutoSizeHdr)
        COMMAND_ID_HANDLER(ID_PFR_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_PFR_SHOWTIMEGMT, OnShowTimeGmt)
        COMMAND_ID_HANDLER(ID_PFR_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_PFR_EXIT, OnExit)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        if (!m_noSaveLoad) m_cfg.load();
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        CreateToolbar();
        m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
        int parts[] = { 300, -1 };
        ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

        m_panel.Create(m_hWnd);
        // The template carries WS_SYSMENU, so strip the caption styles on this docked child.
        m_panel.ModifyStyle(WS_CAPTION | WS_SYSMENU | WS_BORDER | WS_DLGFRAME, WS_CLIPSIBLINGS);
        { CRect pr; m_panel.GetWindowRect(&pr); m_panelHeight = pr.Height(); }
        m_panel.ShowWindow(SW_SHOW);   // template 111 has no WS_VISIBLE, show it explicitly

        m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
            WS_EX_CLIENTEDGE, IDC_LISTVIEW);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

        SetWindowTextW(L"PreviousFilesRecovery");
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        RebuildColumns();
        ApplyGridLines();
        ApplyTooltips();
        SyncMenuChecks();

        m_panel.ApplyConfig(m_cfg);
        m_panel.onStart = [this] { DoScan(); };

        if (m_cfg.havePlacement) SetWindowPlacement(&m_cfg.placement);

        if (CMessageLoop* pLoop = _Module.GetMessageLoop()) pLoop->AddMessageFilter(this);
        SetMsgHandled(FALSE);
        return 0;
    }

    void CreateToolbar()
    {
        CToolBarCtrl tb;
        tb.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0, IDC_TOOLBAR);
        tb.SetButtonStructSize(sizeof(TBBUTTON));
        // Drop the visual style so the flat toolbar does not paint a themed-dark band,
        // and the button-face erase below shows through the masked strip's transparent
        // pixels. OnEraseBkgnd fills the frame with the button-face colour.
        ::SetWindowTheme(tb, L"", L"");
        tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
        // Recovered button order: Recover(5), sep, Save(1), Copy(2), Properties(3), Find(0), Exit(4).
        struct { int bmp, cmd; } btns[] = {
            { 5, ID_PFR_RECOVER }, { -1, 0 },
            { 1, ID_PFR_SAVE }, { 2, ID_PFR_COPY }, { 3, ID_PFR_PROPERTIES },
            { 0, ID_PFR_FIND }, { 4, ID_PFR_EXIT },
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
        m_hWndToolBar = tb;
    }

    int m_panelHeight = 0;

    // Fill the frame with the button-face colour so the transparent flat toolbar band
    // reads light. The opaque panel and ListView paint over their own areas.
    LRESULT OnEraseBkgnd(CDCHandle dc)
    {
        CRect rc; GetClientRect(&rc);
        dc.FillRect(&rc, ::GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    void OnSize(UINT type, CSize)
    {
        // Lay out everything here so the toolbar stays at the top, the search panel
        // docks under it, the status bar stays at the bottom and the ListView fills
        // the rest. Handled fully, so the frame's own client sizing does not reclaim
        // the panel's band.
        if (type == SIZE_MINIMIZED) return;
        if (m_hWndToolBar) ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
        if (m_hWndStatusBar) ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);

        CRect rc; GetClientRect(&rc);
        int top = 0, bottom = rc.bottom;
        if (m_hWndToolBar) { CRect t; ::GetWindowRect(m_hWndToolBar, &t); top += t.Height(); }
        if (m_hWndStatusBar) { CRect s; ::GetWindowRect(m_hWndStatusBar, &s); bottom -= s.Height(); }

        int panelH = m_panelHeight;
        if (panelH > bottom - top) panelH = (bottom - top) > 0 ? bottom - top : 0;

        if (m_panel.m_hWnd) m_panel.SetWindowPos(nullptr, 0, top, rc.right, panelH, SWP_NOZORDER);
        if (m_list.m_hWnd) m_list.SetWindowPos(nullptr, 0, top + panelH, rc.right, bottom - top - panelH, SWP_NOZORDER);
    }

    // ---- columns --------------------------------------------------------
    void RebuildColumns()
    {
        while (m_list.DeleteColumn(0)) {}
        int i = 0;
        for (size_t k = 0; k < m_cfg.visibleColumns.size(); ++k)
        {
            int c = m_cfg.visibleColumns[k];
            int w = k < m_cfg.columnWidths.size() ? m_cfg.columnWidths[k] : kColumns[c].width;
            int fmt = (kColumns[c].kind == ColKind::CK_NUMERIC) ? LVCFMT_RIGHT : LVCFMT_LEFT;
            m_list.InsertColumn(i++, kColumns[c].title, fmt, w);
        }
    }

    void ApplyGridLines()
    {
        DWORD ex = m_list.GetExtendedListViewStyle();
        ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
        m_list.SetExtendedListViewStyle(ex);
    }
    void ApplyTooltips()
    {
        DWORD ex = m_list.GetExtendedListViewStyle();
        ex = m_cfg.showInfoTip ? (ex | LVS_EX_INFOTIP) : (ex & ~LVS_EX_INFOTIP);
        m_list.SetExtendedListViewStyle(ex);
    }

    void SyncMenuChecks()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull()) return;
        auto chk = [&](UINT id, bool on) { menu.CheckMenuItem(id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
        chk(ID_PFR_GRIDLINES, m_cfg.showGridLines);
        chk(ID_PFR_TOOLTIPS, m_cfg.showInfoTip);
        chk(ID_PFR_MARKODDEVEN, m_cfg.markOddEvenRows);
        chk(ID_PFR_SHOWTIMEGMT, m_cfg.showTimeInGmt);
    }

    DisplayOptions disp() const { DisplayOptions d; d.showTimeInGmt = m_cfg.showTimeInGmt; return d; }

    // ---- scan -----------------------------------------------------------
    void DoScan()
    {
        m_panel.ReadConfig(m_cfg);

        ScanOptions opt;
        opt.baseFolder = m_cfg.baseFolder;
        opt.wildcard = m_cfg.filesWildcard;
        opt.depth = m_cfg.subfoldersDepth;
        opt.findPrevVersions = m_cfg.findPreviousFiles;
        opt.findDeletedFiles = m_cfg.findDeletedFiles;
        opt.findDeletedFolders = m_cfg.findDeletedFolders;
        opt.mode = m_cfg.multipleCopiesMode;
        opt.selectedShadow = m_panel.SelectedShadow();
        opt.disp = disp();

        if (opt.baseFolder.size() < 2 || opt.baseFolder[1] != L':')
        {
            MessageBoxW(L"Choose the base folder to scan", L"PreviousFilesRecovery", MB_OK | MB_ICONINFORMATION);
            return;
        }

        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        // ponytail: synchronous scan with a wait cursor, naive O(folders x snapshots)
        // walk. A background thread would keep the UI live on a huge tree, add if a
        // deep scan ever needs to stay responsive.
        int tick = 0;
        m_items = ScanPreviousFiles(opt, [&](int count) {
            if (++tick % 64 == 0) m_panel.SetStatus(L"Loading... " + std::to_wstring(count));
            return true;
        });
        ::SetCursor(prev);

        if (EnumerateShadowCopies(opt.baseFolder[0]).empty())
            m_panel.SetStatus(L"Cannot find shadow copies for this drive !");
        else
            m_panel.SetStatus(L"");

        SortItems();
        m_list.SetItemCount((int)m_items.size());
        m_list.Invalidate();
        UpdateStatus();
    }

    void SortItems()
    {
        const int col = m_cfg.sortColumn, c0 = (col >= 0 && col < kColumnCount) ? col : 0;
        const bool asc = m_cfg.sortAscending;
        DisplayOptions d = disp();
        std::stable_sort(m_items.begin(), m_items.end(), [&](const PrevFileRecord& a, const PrevFileRecord& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(c0, na), kb = b.sortKey(c0, nb);
            int cmp;
            if (na && nb) cmp = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          cmp = _wcsicmp(a.column(c0, d).c_str(), b.column(c0, d).c_str());
            return asc ? (cmp < 0) : (cmp > 0);
        });
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        int disp = reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem;
        if (disp < 0 || disp >= (int)m_cfg.visibleColumns.size()) return 0;
        int clicked = m_cfg.visibleColumns[disp];
        if (clicked == m_cfg.sortColumn) m_cfg.sortAscending = !m_cfg.sortAscending;
        else { m_cfg.sortColumn = clicked; m_cfg.sortAscending = true; }
        SortItems();
        m_list.Invalidate();
        return 0;
    }

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        int item = di->item.iItem;
        if (item < 0 || item >= (int)m_items.size()) return 0;
        if (di->item.mask & LVIF_TEXT)
        {
            int col = m_cfg.visibleColumns[di->item.iSubItem];
            static std::wstring text;
            text = m_items[item].column(col, disp());
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
        return 0;
    }

    LRESULT OnStateChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }

    void UpdateStatus()
    {
        wchar_t buf[128];
        int sel = m_list.GetSelectedCount();
        if (sel > 0) wsprintfW(buf, L"%d item(s), %d Selected", (int)m_items.size(), sel);
        else         wsprintfW(buf, L"%d item(s)", (int)m_items.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
    }

    // ---- selection ------------------------------------------------------
    std::vector<PrevFileRecord> SelectedItems()
    {
        std::vector<PrevFileRecord> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            if (i < (int)m_items.size()) v.push_back(m_items[i]);
        return v;
    }

    ExportRequest makeRequest(const std::vector<PrevFileRecord>& items)
    {
        ExportRequest r;
        r.columns = m_cfg.visibleColumns;
        r.rowCount = (int)items.size();
        DisplayOptions d = disp();
        r.cell = [items, d](int row, int col) { return items[row].column(col, d); };
        return r;
    }

    // ---- commands -------------------------------------------------------
    LRESULT OnStartScan(WORD, WORD, HWND, BOOL&) { DoScan(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED); UpdateStatus(); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, 0, LVIS_SELECTED); UpdateStatus(); return 0; }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_cfg.showGridLines = !m_cfg.showGridLines; ApplyGridLines(); SyncMenuChecks(); return 0; }
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&) { m_cfg.showInfoTip = !m_cfg.showInfoTip; ApplyTooltips(); SyncMenuChecks(); return 0; }
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&) { m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows; m_list.Invalidate(); SyncMenuChecks(); return 0; }
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&) { m_cfg.showTimeInGmt = !m_cfg.showTimeInGmt; SyncMenuChecks(); m_list.Invalidate(); return 0; }

    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&)
    { for (int i = 0; i < (int)m_cfg.visibleColumns.size(); ++i) m_list.SetColumnWidth(i, LVSCW_AUTOSIZE); SyncWidths(); return 0; }
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&)
    { for (int i = 0; i < (int)m_cfg.visibleColumns.size(); ++i) m_list.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER); SyncWidths(); return 0; }
    void SyncWidths()
    { for (size_t i = 0; i < m_cfg.visibleColumns.size(); ++i) if (i < m_cfg.columnWidths.size()) m_cfg.columnWidths[i] = m_list.GetColumnWidth((int)i); }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) return 0;
        CopyToClipboard(BuildReport(makeRequest(sel), ExportFormat::Tab));
        return 0;
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        std::vector<PrevFileRecord>& items = sel.empty() ? m_items : sel;
        CFileDialog dlg(FALSE, L"txt", L"PreviousFilesRecovery.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
            L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.html)\0*.html\0"
            L"HTML File - Vertical (*.html)\0*.html\0XML File (*.xml)\0*.xml\0"
            L"Comma Delimited Text File (*.csv)\0*.csv\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        if (m_cfg.saveFilterIndex > 0) dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex;
        if (dlg.DoModal() != IDOK) return 0;
        m_cfg.saveFilterIndex = dlg.m_ofn.nFilterIndex;
        WriteReport(dlg.m_szFileName, makeRequest(items), FormatFromFilterIndex(dlg.m_ofn.nFilterIndex));
        return 0;
    }

    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(m_items); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&) { auto s = SelectedItems(); ShowHtml(s.empty() ? m_items : s); return 0; }

    void ShowHtml(const std::vector<PrevFileRecord>& items)
    {
        wchar_t dir[MAX_PATH], tmp[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"pfr", 0, tmp);
        std::wstring html = tmp; html += L".html";
        ExportRequest r = makeRequest(items);
        r.encoding = 2;
        WriteReport(html, r, ExportFormat::HtmlHorizontal);
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        CColumnsDlg dlg(m_cfg.visibleColumns, m_cfg.columnWidths);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            m_cfg.visibleColumns = dlg.m_visible;
            m_cfg.columnWidths = dlg.m_widths;
            if (std::find(m_cfg.visibleColumns.begin(), m_cfg.visibleColumns.end(), m_cfg.sortColumn) == m_cfg.visibleColumns.end()
                && !m_cfg.visibleColumns.empty())
                m_cfg.sortColumn = m_cfg.visibleColumns.front();
            RebuildColumns();
            m_list.SetItemCount((int)m_items.size());
            m_list.Invalidate();
        }
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_items.size()) return 0;
        CPropertiesDlg dlg(m_items[i], m_cfg.visibleColumns, disp());
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnRecover(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) return 0;
        CRecoverDlg dlg(sel);
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

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
        if (what.empty() || m_items.empty()) return;
        std::wstring needle = what;
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
        int n = (int)m_items.size();
        DisplayOptions d = disp();
        for (int step = 1; step <= n; ++step)
        {
            int i = down ? (start + step) % n : ((start - step) % n + n) % n;
            for (int c : m_cfg.visibleColumns)
            {
                std::wstring v = m_items[i].column(c, d);
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

    // ---- notifications --------------------------------------------------
    LRESULT OnDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnProperties(0, 0, nullptr, b); return 0; }

    LRESULT OnRClick(int, LPNMHDR p, BOOL&)
    {
        auto* ia = reinterpret_cast<LPNMITEMACTIVATE>(p);
        m_rclickRow = ia->iItem;
        m_rclickCol = ia->iSubItem;
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CONTEXT_MENU));
        if (!menu) return 0;
        HMENU sub = ::GetSubMenu(menu, 0);
        POINT pt; ::GetCursorPos(&pt);
        ::TrackPopupMenu(sub, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
        ::DestroyMenu(menu);
        return 0;
    }

    void CopyToClipboard(const std::wstring& text)
    {
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
            m_panel.ReadConfig(m_cfg);
            SyncWidths();
            m_cfg.placement.length = sizeof(m_cfg.placement);
            if (GetWindowPlacement(&m_cfg.placement)) m_cfg.havePlacement = true;
            m_cfg.save();
        }
        PostQuitMessage(0);
    }
};
