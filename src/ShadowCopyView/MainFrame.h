// MainFrame.h -- WTL main window for the ShadowCopyView recreation. The original
// registers a plain window class and hangs two SysListView32 controls, a toolbar, a
// status bar and an editable path band directly off it, with a draggable horizontal
// splitter between the snapshot list (top) and the file list (bottom). This rebuilds
// that layout cleanly over the EnumerateSnapshots and EnumerateFiles model.
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
#include "VssEnumerator.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"ShadowCopyView", IDR_MAIN_MENU)

    CListViewCtrl m_snapList;
    CListViewCtrl m_fileList;
    CEdit         m_filePath;

    Config m_cfg;
    bool m_noSaveLoad = false;

    std::vector<SnapshotRecord> m_snaps;
    std::vector<FileRecord>     m_files;
    std::wstring m_curFolder;         // folder inside the selected snapshot, empty is root
    int m_selSnap = -1;

    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    int m_snapPaneH = 0;              // snapshot pane height, drives the splitter
    bool m_dragging = false;
    static const int kSplitBar = 6;
    static const int kPathH = 23;

    DisplayOptions disp() const { DisplayOptions d; d.showTimeInGmt = m_cfg.showTimeInGmt; return d; }
    bool focusIsFile() const { return ::GetFocus() == m_fileList.m_hWnd || ::GetFocus() == m_filePath.m_hWnd; }

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg)) return TRUE;
        // Enter inside the path edit navigates to the typed folder.
        if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN &&
            pMsg->hwnd == m_filePath.m_hWnd)
        {
            NavigateFromPathEdit();
            return TRUE;
        }
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        if (uMsg == m_uFindMsg) { OnFindReplaceMsg(lParam); lResult = 0; return TRUE; }
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
        NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClk)
        NOTIFY_CODE_HANDLER(NM_RCLICK, OnRClick)
        NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
        COMMAND_ID_HANDLER(ID_SCV_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_SCV_OPEN_EXPLORER, OnOpenExplorer)
        COMMAND_ID_HANDLER(ID_SCV_LEVELUP, OnLevelUp)
        COMMAND_ID_HANDLER(ID_SCV_GOTOROOT, OnGotoRoot)
        COMMAND_ID_HANDLER(ID_SCV_COPYFILESTO, OnCopyFilesTo)
        COMMAND_ID_HANDLER(ID_SCV_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_SCV_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_SCV_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_SCV_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_SCV_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_SCV_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_SCV_CHOOSECOLS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_SCV_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_SCV_AUTOSIZE_HDR, OnAutoSizeHdr)
        COMMAND_ID_HANDLER(ID_SCV_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_SCV_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_SCV_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_SCV_TOOLTIPS, OnTooltips)
        COMMAND_ID_HANDLER(ID_SCV_MARKODDEVEN, OnMarkOddEven)
        COMMAND_ID_HANDLER(ID_SCV_SHOWTIMEGMT, OnShowTimeGmt)
        COMMAND_ID_HANDLER(ID_SCV_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_SCV_EXIT, OnExit)
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
        int parts[] = { 220, 440, -1 };
        ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 3, (LPARAM)parts);

        m_snapList.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, IDC_SNAPLIST);
        m_fileList.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, IDC_FILELIST);
        m_filePath.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, IDC_FILEPATH);
        m_filePath.SetFont((HFONT)::GetStockObject(DEFAULT_GUI_FONT));

        m_snapList.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);
        m_fileList.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

        SetWindowTextW(L"ShadowCopyView");
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        RebuildColumns(m_snapList, m_cfg.snapCols, kSnapColumns);
        RebuildColumns(m_fileList, m_cfg.fileCols, kFileColumns);
        ApplyGridLines();

        if (m_cfg.verSplitLoc > 0) m_snapPaneH = m_cfg.verSplitLoc;

        if (m_cfg.havePlacement) SetWindowPlacement(&m_cfg.placement);

        if (CMessageLoop* pLoop = _Module.GetMessageLoop()) pLoop->AddMessageFilter(this);

        SetMsgHandled(FALSE);
        PostMessage(WM_COMMAND, MAKEWPARAM(ID_SCV_REFRESH, 0));
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
        struct { int bmp, cmd; } btns[] = {
            { -1, 0 },
            { 8, ID_SCV_LEVELUP }, { 9, ID_SCV_GOTOROOT }, { 7, ID_SCV_COPYFILESTO },
            { -1, 0 },
            { 6, ID_SCV_OPEN_EXPLORER }, { 1, ID_SCV_SAVE }, { 5, ID_SCV_REFRESH },
            { 2, ID_SCV_COPY }, { 3, ID_SCV_PROPERTIES }, { 0, ID_SCV_FIND }, { 4, ID_SCV_EXIT },
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
    int toolbarHeight()
    {
        if (!m_hWndToolBar) return 0;
        CRect rc; ::GetWindowRect(m_hWndToolBar, &rc);
        return rc.Height();
    }
    int statusHeight()
    {
        if (!m_hWndStatusBar) return 0;
        CRect rc; ::GetWindowRect(m_hWndStatusBar, &rc);
        return rc.Height();
    }

    void OnSize(UINT type, CSize)
    {
        if (m_hWndToolBar) ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
        if (m_hWndStatusBar) ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);

        CRect rc; GetClientRect(&rc);
        int top = toolbarHeight();
        int bottom = rc.bottom - statusHeight();
        int usable = bottom - top;
        if (usable < 80) return;

        if (m_snapPaneH <= 0) m_snapPaneH = usable * 2 / 5;
        int snapH = std::clamp(m_snapPaneH, 40, usable - (kSplitBar + kPathH + 40));
        m_snapPaneH = snapH;

        int w = rc.right;
        int y = top;
        m_snapList.SetWindowPos(nullptr, 0, y, w, snapH, SWP_NOZORDER);
        y += snapH + kSplitBar;
        m_filePath.SetWindowPos(nullptr, 0, y, w, kPathH, SWP_NOZORDER);
        y += kPathH;
        m_fileList.SetWindowPos(nullptr, 0, y, w, bottom - y, SWP_NOZORDER);
    }

    CRect splitterRect()
    {
        CRect rc; GetClientRect(&rc);
        int top = toolbarHeight();
        int y = top + m_snapPaneH;
        return CRect(0, y, rc.right, y + kSplitBar);
    }

    BOOL OnSetCursor(CWindow wnd, UINT hitTest, UINT message)
    {
        if (wnd.m_hWnd == m_hWnd && hitTest == HTCLIENT)
        {
            POINT pt; ::GetCursorPos(&pt); ScreenToClient(&pt);
            if (splitterRect().PtInRect(pt)) { ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS)); return TRUE; }
        }
        SetMsgHandled(FALSE);
        return FALSE;
    }
    void OnLButtonDown(UINT, CPoint pt)
    {
        if (splitterRect().PtInRect(pt)) { m_dragging = true; SetCapture(); }
        else SetMsgHandled(FALSE);
    }
    void OnMouseMove(UINT, CPoint pt)
    {
        if (!m_dragging) { SetMsgHandled(FALSE); return; }
        int top = toolbarHeight();
        m_snapPaneH = pt.y - top;
        OnSize(0, CSize());
    }
    void OnLButtonUp(UINT, CPoint)
    {
        if (m_dragging) { m_dragging = false; ReleaseCapture(); }
        else SetMsgHandled(FALSE);
    }

    // ---- columns --------------------------------------------------------
    void RebuildColumns(CListViewCtrl& list, const ColumnState& st, const ColumnDef* defs)
    {
        while (list.DeleteColumn(0)) {}
        int i = 0;
        for (size_t k = 0; k < st.visible.size(); ++k)
        {
            int c = st.visible[k];
            int w = k < st.widths.size() ? st.widths[k] : defs[c].width;
            list.InsertColumn(i++, defs[c].title, defs[c].format, w);
        }
    }

    void ApplyGridLines()
    {
        for (CListViewCtrl* l : { &m_snapList, &m_fileList })
        {
            DWORD ex = l->GetExtendedListViewStyle();
            ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
            l->SetExtendedListViewStyle(ex);
        }
    }

    // ---- data -----------------------------------------------------------
    void Refresh()
    {
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        HRESULT hr = S_OK;
        m_snaps = EnumerateSnapshots(disp(), &hr);
        SortList(m_snaps, m_cfg.snapCols);
        m_snapList.SetItemCount((int)m_snaps.size());
        m_snapList.Invalidate();
        m_selSnap = -1;
        m_curFolder.clear();
        m_files.clear();
        m_fileList.SetItemCount(0);
        m_fileList.Invalidate();
        m_filePath.SetWindowTextW(L"");
        UpdateStatus();
        ::SetCursor(prev);
        if (!m_snaps.empty())
        {
            m_snapList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }

    void LoadFiles()
    {
        if (m_selSnap < 0 || m_selSnap >= (int)m_snaps.size()) { m_files.clear(); m_fileList.SetItemCount(0); m_fileList.Invalidate(); UpdateStatus(); return; }
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        m_files = EnumerateFiles(m_snaps[m_selSnap], m_curFolder, disp());
        SortList(m_files, m_cfg.fileCols);
        m_fileList.SetItemCount((int)m_files.size());
        m_fileList.Invalidate();
        m_filePath.SetWindowTextW(m_curFolder.empty() ? L"\\" : m_curFolder.c_str());
        UpdateStatus();
        ::SetCursor(prev);
    }

    template <class Rec>
    void SortList(std::vector<Rec>& items, const ColumnState& st)
    {
        int col = st.sortColumn;
        bool asc = st.sortAscending;
        std::stable_sort(items.begin(), items.end(), [&](const Rec& a, const Rec& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
            int c;
            if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    // ---- navigation -----------------------------------------------------
    void NavigateInto(const std::wstring& folderName)
    {
        m_curFolder += L"\\";
        m_curFolder += folderName;
        LoadFiles();
    }
    void NavigateFromPathEdit()
    {
        wchar_t buf[2048]{};
        m_filePath.GetWindowTextW(buf, 2048);
        std::wstring p = buf;
        while (!p.empty() && (p.back() == L'\\' || p.back() == L' ')) p.pop_back();
        if (!p.empty() && p[0] != L'\\') p = L"\\" + p;
        m_curFolder = p;
        LoadFiles();
        m_fileList.SetFocus();
    }

    // ---- owner-data -----------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        static std::wstring text;
        if (!(di->item.mask & LVIF_TEXT)) return 0;
        int item = di->item.iItem;
        if (p->idFrom == IDC_SNAPLIST)
        {
            if (item < 0 || item >= (int)m_snaps.size()) return 0;
            int col = m_cfg.snapCols.visible[di->item.iSubItem];
            text = m_snaps[item].column(col);
        }
        else
        {
            if (item < 0 || item >= (int)m_files.size()) return 0;
            int col = m_cfg.fileCols.visible[di->item.iSubItem];
            text = m_files[item].column(col);
        }
        di->item.pszText = const_cast<LPWSTR>(text.c_str());
        return 0;
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        auto* nv = reinterpret_cast<LPNMLISTVIEW>(p);
        bool snap = (p->idFrom == IDC_SNAPLIST);
        ColumnState& st = snap ? m_cfg.snapCols : m_cfg.fileCols;
        int disp = nv->iSubItem;
        if (disp < 0 || disp >= (int)st.visible.size()) return 0;
        int clicked = st.visible[disp];
        if (clicked == st.sortColumn) st.sortAscending = !st.sortAscending;
        else { st.sortColumn = clicked; st.sortAscending = true; }
        if (snap) { SortList(m_snaps, st); m_snapList.Invalidate(); }
        else      { SortList(m_files, st); m_fileList.Invalidate(); }
        return 0;
    }

    LRESULT OnItemChanged(int, LPNMHDR p, BOOL&)
    {
        if (p->idFrom == IDC_SNAPLIST)
        {
            int sel = m_snapList.GetNextItem(-1, LVNI_SELECTED);
            if (sel != m_selSnap)
            {
                m_selSnap = sel;
                m_curFolder.clear();
                LoadFiles();
            }
        }
        UpdateStatus();
        return 0;
    }

    LRESULT OnDblClk(int, LPNMHDR p, BOOL&)
    {
        if (p->idFrom == IDC_FILELIST)
        {
            int i = m_fileList.GetNextItem(-1, LVNI_FOCUSED);
            if (i >= 0 && i < (int)m_files.size() && m_files[i].isDir) NavigateInto(m_files[i].name);
        }
        else
        {
            BOOL b; OnProperties(0, 0, nullptr, b);
        }
        return 0;
    }

    LRESULT OnListKeyDown(int, LPNMHDR p, BOOL&)
    {
        auto* kd = reinterpret_cast<LPNMLVKEYDOWN>(p);
        if (p->idFrom == IDC_FILELIST && kd->wVKey == VK_RETURN)
        {
            int i = m_fileList.GetNextItem(-1, LVNI_FOCUSED);
            if (i >= 0 && i < (int)m_files.size() && m_files[i].isDir) NavigateInto(m_files[i].name);
        }
        return 0;
    }

    LRESULT OnRClick(int, LPNMHDR p, BOOL&)
    {
        UINT menuId = (p->idFrom == IDC_FILELIST) ? IDR_CTX_FILE : IDR_CTX_SNAPSHOT;
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(menuId));
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
        int selS = m_snapList.GetSelectedCount();
        if (selS > 0) swprintf(buf, 128, L"%d item(s), %d Selected", (int)m_snaps.size(), selS);
        else          swprintf(buf, 128, L"%d item(s)", (int)m_snaps.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);

        swprintf(buf, 128, L"%d Files+Folders", (int)m_files.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)buf);
    }

    // ---- selection helpers ---------------------------------------------
    std::vector<int> selectedRows(CListViewCtrl& list)
    {
        std::vector<int> v;
        for (int i = list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = list.GetNextItem(i, LVNI_SELECTED))
            v.push_back(i);
        return v;
    }

    ExportRequest buildRequest(bool snap)
    {
        ExportRequest r;
        if (snap)
        {
            r.colDefs = kSnapColumns;
            r.columns = m_cfg.snapCols.visible;
            r.reportTitle = L"Shadow Copies List";
            r.rowCount = (int)m_snaps.size();
            r.cell = [this](int row, int col) { return m_snaps[row].column(col); };
        }
        else
        {
            r.colDefs = kFileColumns;
            r.columns = m_cfg.fileCols.visible;
            r.reportTitle = L"Files List In Shadow Copy";
            r.rowCount = (int)m_files.size();
            r.cell = [this](int row, int col) { return m_files[row].column(col); };
        }
        return r;
    }

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    LRESULT OnOpenExplorer(WORD, WORD, HWND, BOOL&)
    {
        int i = m_snapList.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_snaps.size()) return 0;
        const std::wstring& ep = m_snaps[i].explorerPath;
        if (ep.empty())
        {
            MessageBoxW(L"This snapshot cannot be opened in Windows Explorer.", L"ShadowCopyView", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        ::ShellExecuteW(m_hWnd, L"open", ep.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    LRESULT OnLevelUp(WORD, WORD, HWND, BOOL&)
    {
        if (m_curFolder.empty()) return 0;
        size_t slash = m_curFolder.rfind(L'\\');
        m_curFolder = (slash == std::wstring::npos) ? std::wstring() : m_curFolder.substr(0, slash);
        LoadFiles();
        return 0;
    }
    LRESULT OnGotoRoot(WORD, WORD, HWND, BOOL&) { m_curFolder.clear(); LoadFiles(); return 0; }

    LRESULT OnCopyFilesTo(WORD, WORD, HWND, BOOL&)
    {
        if (m_selSnap < 0) return 0;
        std::vector<std::wstring> names;
        for (int i : selectedRows(m_fileList)) names.push_back(m_files[i].name);
        if (names.empty()) return 0;
        CCopyFilesDlg dlg(m_snaps[m_selSnap], m_curFolder, names);
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        bool snap = !focusIsFile();
        CFileDialog dlg(FALSE, L"txt", snap ? L"ShadowCopies.txt" : L"Files.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
            L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.html)\0*.html\0"
            L"HTML File - Vertical (*.html)\0*.html\0XML File (*.xml)\0*.xml\0"
            L"Comma Delimited Text File (*.csv)\0*.csv\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex;
        if (dlg.DoModal() != IDOK) return 0;
        m_cfg.saveFilterIndex = dlg.m_ofn.nFilterIndex;
        ExportFormat fmt = FormatFromFilterIndex(dlg.m_ofn.nFilterIndex);
        WriteReport(dlg.m_szFileName, buildRequest(snap), fmt);
        return 0;
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        bool snap = !focusIsFile();
        CListViewCtrl& list = snap ? m_snapList : m_fileList;
        auto rows = selectedRows(list);
        if (rows.empty()) return 0;
        ExportRequest base = buildRequest(snap);
        ExportRequest r = base;
        r.rowCount = (int)rows.size();
        r.cell = [&](int row, int col) { return base.cell(rows[row], col); };
        CopyToClipboard(BuildReport(r, ExportFormat::Tab));
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        bool snap = !focusIsFile();
        std::vector<std::pair<std::wstring, std::wstring>> rows;
        if (snap)
        {
            int i = m_snapList.GetNextItem(-1, LVNI_FOCUSED);
            if (i < 0 || i >= (int)m_snaps.size()) return 0;
            for (int c : m_cfg.snapCols.visible) rows.emplace_back(kSnapColumns[c].title, m_snaps[i].column(c));
        }
        else
        {
            int i = m_fileList.GetNextItem(-1, LVNI_FOCUSED);
            if (i < 0 || i >= (int)m_files.size()) return 0;
            for (int c : m_cfg.fileCols.visible) rows.emplace_back(kFileColumns[c].title, m_files[i].column(c));
        }
        CPropertiesDlg(std::move(rows)).DoModal(m_hWnd);
        return 0;
    }

    void ShowHtml(bool snap, bool selectedOnly)
    {
        ExportRequest r = buildRequest(snap);
        if (selectedOnly)
        {
            CListViewCtrl& list = snap ? m_snapList : m_fileList;
            auto rows = selectedRows(list);
            if (!rows.empty())
            {
                ExportRequest base = buildRequest(snap);
                r.rowCount = (int)rows.size();
                r.cell = [base, rows](int row, int col) { return base.cell(rows[row], col); };
            }
        }
        wchar_t dir[MAX_PATH], tmp[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"scv", 0, tmp);
        std::wstring html = tmp; html += L".html";
        WriteReport(html, r, ExportFormat::HtmlHorizontal);
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(!focusIsFile(), false); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&) { ShowHtml(!focusIsFile(), true); return 0; }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        bool snap = !focusIsFile();
        const ColumnDef* defs = snap ? kSnapColumns : kFileColumns;
        int count = snap ? kSnapColumnCount : kFileColumnCount;
        ColumnState& st = snap ? m_cfg.snapCols : m_cfg.fileCols;
        CColumnsDlg dlg(defs, count, st);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            st = dlg.m_state;
            if (std::find(st.visible.begin(), st.visible.end(), st.sortColumn) == st.visible.end() && !st.visible.empty())
                st.sortColumn = st.visible.front();
            RebuildColumns(snap ? m_snapList : m_fileList, st, defs);
            (snap ? m_snapList : m_fileList).Invalidate();
        }
        return 0;
    }

    void AutoSize(int how)
    {
        bool snap = !focusIsFile();
        CListViewCtrl& list = snap ? m_snapList : m_fileList;
        ColumnState& st = snap ? m_cfg.snapCols : m_cfg.fileCols;
        for (int i = 0; i < (int)st.visible.size(); ++i) list.SetColumnWidth(i, how);
        for (size_t i = 0; i < st.visible.size(); ++i)
            if (i < st.widths.size()) st.widths[i] = list.GetColumnWidth((int)i);
    }
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&) { AutoSize(LVSCW_AUTOSIZE); return 0; }
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&) { AutoSize(LVSCW_AUTOSIZE_USEHEADER); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&)
    {
        CListViewCtrl& list = focusIsFile() ? m_fileList : m_snapList;
        list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
        UpdateStatus();
        return 0;
    }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&)
    {
        CListViewCtrl& list = focusIsFile() ? m_fileList : m_snapList;
        list.SetItemState(-1, 0, LVIS_SELECTED);
        UpdateStatus();
        return 0;
    }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_cfg.showGridLines = !m_cfg.showGridLines; ApplyGridLines(); SyncChecks(); return 0; }
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&) { m_cfg.showInfoTip = !m_cfg.showInfoTip; SyncChecks(); return 0; }
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&) { m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows; m_snapList.Invalidate(); m_fileList.Invalidate(); SyncChecks(); return 0; }
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&)
    {
        m_cfg.showTimeInGmt = !m_cfg.showTimeInGmt;
        SyncChecks();
        int savedSnap = m_selSnap;
        std::wstring savedFolder = m_curFolder;
        Refresh();
        if (savedSnap >= 0 && savedSnap < (int)m_snaps.size())
        {
            m_selSnap = savedSnap;
            m_curFolder = savedFolder;
            m_snapList.SetItemState(savedSnap, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            LoadFiles();
        }
        return 0;
    }

    void SyncChecks()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull()) return;
        auto chk = [&](UINT id, bool on) { menu.CheckMenuItem(id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
        chk(ID_SCV_GRIDLINES, m_cfg.showGridLines);
        chk(ID_SCV_TOOLTIPS, m_cfg.showInfoTip);
        chk(ID_SCV_MARKODDEVEN, m_cfg.markOddEvenRows);
        chk(ID_SCV_SHOWTIMEGMT, m_cfg.showTimeInGmt);
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
        bool snap = !focusIsFile();
        CListViewCtrl& list = snap ? m_snapList : m_fileList;
        int n = snap ? (int)m_snaps.size() : (int)m_files.size();
        if (what.empty() || n == 0) return;
        std::wstring needle = what;
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        const std::vector<int>& cols = snap ? m_cfg.snapCols.visible : m_cfg.fileCols.visible;
        int start = list.GetNextItem(-1, LVNI_FOCUSED);
        for (int step = 1; step <= n; ++step)
        {
            int i = down ? (start + step) % n : ((start - step) % n + n) % n;
            for (int c : cols)
            {
                std::wstring v = snap ? m_snaps[i].column(c) : m_files[i].column(c);
                std::transform(v.begin(), v.end(), v.begin(), towlower);
                if (v.find(needle) != std::wstring::npos)
                {
                    list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    list.EnsureVisible(i, FALSE);
                    return;
                }
            }
        }
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
            for (size_t i = 0; i < m_cfg.snapCols.visible.size(); ++i)
                if (i < m_cfg.snapCols.widths.size()) m_cfg.snapCols.widths[i] = m_snapList.GetColumnWidth((int)i);
            for (size_t i = 0; i < m_cfg.fileCols.visible.size(); ++i)
                if (i < m_cfg.fileCols.widths.size()) m_cfg.fileCols.widths[i] = m_fileList.GetColumnWidth((int)i);
            m_cfg.verSplitLoc = m_snapPaneH;
            m_cfg.placement.length = sizeof(m_cfg.placement);
            if (GetWindowPlacement(&m_cfg.placement)) m_cfg.havePlacement = true;
            m_cfg.save();
        }
        PostQuitMessage(0);
    }
};
