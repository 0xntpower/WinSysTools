// MainFrame.h -- WTL main window for the FileTypesMan recreation.
// Two stacked list panes (the upper lists every file type / extension, the lower lists
// the actions of the selected type), a menu (102), an 8-image toolbar (104), a status
// bar and the extracted dialogs. The data model comes from EnumerateFileTypes /
// EnumerateActions rather than the framework code. Config lives in FileTypesMan.cfg.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlsplit.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "FileType.h"
#include "FileAction.h"
#include "Columns.h"
#include "Export.h"
#include "Dialogs.h"

extern CAppModule _Module;

struct AppOptions
{
    bool showGridLines   = false;  // ShowGridLines 0
    bool showInfoTip     = true;   // ShowInfoTip 1
    bool dontLoadIcons   = false;  // DontLoadIcons 0
    bool blackBackground = false;  // BlackBackground 0
    bool dontLoadHang    = true;   // DontLoadHangIcons 1
    bool markDisabled    = true;   // MarkDisabledActions 1
    bool autoDeskRefresh = true;   // AutoDesktopRefresh 1
    bool showUrl         = false;  // ShowURLProtocols 0
    bool extractCaption  = true;   // ExtractResourceCaption 1
    bool showApps        = true;   // ShowApplicationsTypes 1
    bool removeDot       = false;  // RemoveExtensionDot 0
    bool addHeaderLine   = false;  // AddExportHeaderLine 0
    bool markOddEven     = false;  // MarkOddEvenRows 0
    int  saveFilterIndex = 1;      // SaveFilterIndex
};

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"FileTypesMan", IDR_MAIN_MENU)

    CHorSplitterWindow m_split;
    CListViewCtrl m_types, m_actions;
    CImageList    m_images;

    std::vector<FileType>   m_items;
    std::vector<FileAction> m_acts;
    std::vector<int> m_typeVis, m_actVis;
    int m_typeW[kTypeColumnCount];
    int m_actW[kActionColumnCount];
    int  m_typeSort = 0, m_actSort = 0;
    bool m_typeAsc = true, m_actAsc = true;

    AppOptions m_opt;
    bool m_noSaveLoad = false;
    int  m_lastFocus = IDC_LIST_TYPES;
    int  m_selectedType = -1;
    std::wstring m_startSelectExt;
    std::wstring m_cfgPath;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    CMainFrame()
    {
        for (int i = 0; i < kTypeColumnCount; ++i) { m_typeVis.push_back(i); m_typeW[i] = kTypeColumns[i].width; }
        for (int i = 0; i < kActionColumnCount; ++i) { m_actVis.push_back(i); m_actW[i] = kActionColumns[i].width; }
        m_cfgPath = ConfigPath();
    }

    static std::wstring ConfigPath()
    {
        wchar_t exe[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        size_t dot = p.find_last_of(L'.');
        if (dot != std::wstring::npos) p.resize(dot);
        return p + L".cfg";
    }

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg)) return TRUE;
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MESSAGE_HANDLER(m_findMsg, OnFindReplace)
        NOTIFY_HANDLER(IDC_LIST_TYPES, LVN_GETDISPINFO, OnTypesDispInfo)
        NOTIFY_HANDLER(IDC_LIST_TYPES, LVN_COLUMNCLICK, OnTypesColumnClick)
        NOTIFY_HANDLER(IDC_LIST_TYPES, LVN_ITEMCHANGED, OnTypesChanged)
        NOTIFY_HANDLER(IDC_LIST_TYPES, NM_SETFOCUS, OnTypesFocus)
        NOTIFY_HANDLER(IDC_LIST_TYPES, NM_RCLICK, OnTypesRClick)
        NOTIFY_HANDLER(IDC_LIST_TYPES, NM_DBLCLK, OnTypesDblClk)
        NOTIFY_HANDLER(IDC_LIST_ACTIONS, LVN_GETDISPINFO, OnActionsDispInfo)
        NOTIFY_HANDLER(IDC_LIST_ACTIONS, LVN_COLUMNCLICK, OnActionsColumnClick)
        NOTIFY_HANDLER(IDC_LIST_ACTIONS, NM_SETFOCUS, OnActionsFocus)
        NOTIFY_HANDLER(IDC_LIST_ACTIONS, NM_RCLICK, OnActionsRClick)
        COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_EDIT_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_VIEW_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE_HDR, OnAutoSize)
        COMMAND_ID_HANDLER(ID_VIEW_COLUMNS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_VIEW_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_VIEW_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_EDIT_FILETYPE_REGEDIT, OnTypeRegEdit)
        COMMAND_ID_HANDLER(ID_ACTION_REGEDIT, OnActionRegEdit)
        COMMAND_ID_HANDLER(ID_EDIT_DELETEEXT, OnDeleteExt)
        COMMAND_ID_HANDLER(ID_EDIT_NEWEXT, OnNewExt)
        COMMAND_ID_HANDLER(ID_ACTION_DELETE, OnDeleteActions)
        COMMAND_ID_HANDLER(ID_HELP_WEBPAGE, OnWebPage)
        COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
        COMMAND_ID_HANDLER(ID_VIEW_TOOLTIPS, OnToggleTooltips)
        COMMAND_ID_HANDLER(ID_OPT_DONTLOADICONS, OnToggleDontLoadIcons)
        COMMAND_ID_HANDLER(ID_OPT_DONTLOADHANG, OnToggleDontLoadHang)
        COMMAND_ID_HANDLER(ID_OPT_MARKDISABLED, OnToggleMarkDisabled)
        COMMAND_ID_HANDLER(ID_OPT_AUTODESKREFRESH, OnToggleAutoDesk)
        COMMAND_ID_HANDLER(ID_OPT_SHOWURL, OnToggleShowUrl)
        COMMAND_ID_HANDLER(ID_OPT_EXTRACTCAPTION, OnToggleExtractCaption)
        COMMAND_ID_HANDLER(ID_OPT_SHOWAPPS, OnToggleShowApps)
        COMMAND_ID_HANDLER(ID_OPT_HIDEDOT, OnToggleHideDot)
        COMMAND_ID_HANDLER(ID_OPT_ADDHEADER, OnToggleAddHeader)
        COMMAND_ID_HANDLER(ID_VIEW_MARKODDEVEN, OnToggleMarkOddEven)
        COMMAND_ID_HANDLER(ID_VIEW_BLACKBG, OnToggleBlackBg)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()
    LRESULT OnEraseBkgnd(CDCHandle dc) { return FillFrameBtnFace(m_hWnd, dc); }

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));
        CreateToolbar();
        m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
        { int parts[2] = { 300, -1 }; ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, (LPARAM)parts); }

        m_hWndClient = m_split.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
        DWORD lvStyle = WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA;
        m_types.Create(m_split, rcDefault, nullptr, lvStyle, WS_EX_CLIENTEDGE, IDC_LIST_TYPES);
        m_actions.Create(m_split, rcDefault, nullptr, lvStyle, WS_EX_CLIENTEDGE, IDC_LIST_ACTIONS);
        m_types.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        m_actions.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        m_split.SetSplitterPanes(m_types, m_actions);
        m_split.SetSplitterPosPct(33);   // VerSplitLoc 21844/65535

        m_images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 2, 2);
        if (HICON hi = (HICON)::LoadImageW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_SMALL),
                                           IMAGE_ICON, 16, 16, 0))
            m_images.AddIcon(hi);
        m_types.SetImageList(m_images, LVSIL_SMALL);
        m_actions.SetImageList(m_images, LVSIL_SMALL);

        SetWindowTextW(L"FileTypesMan");
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        { SetIcon(hi, TRUE); SetIcon(hi, FALSE); }

        LoadConfig();
        RebuildColumns(m_types, kTypeColumns, m_typeVis, m_typeW);
        RebuildColumns(m_actions, kActionColumns, m_actVis, m_actW);
        ApplyGridLines();
        UpdateMenuChecks();

        if (CMessageLoop* pLoop = _Module.GetMessageLoop()) pLoop->AddMessageFilter(this);
        Refresh();
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
        TBBUTTON b[9] = {};
        int n = 0;
        auto add = [&](int bmp, int cmd) {
            b[n].iBitmap = bmp; b[n].idCommand = cmd; b[n].fsState = TBSTATE_ENABLED; b[n].fsStyle = TBSTYLE_BUTTON; ++n;
        };
        auto sep = [&] { b[n].fsStyle = TBSTYLE_SEP; ++n; };
        add(1, ID_FILE_SAVE);
        add(5, ID_VIEW_REFRESH);
        add(2, ID_EDIT_COPY);
        add(3, ID_FILE_PROPERTIES);
        add(0, ID_EDIT_FIND);
        sep();
        add(6, ID_TB_SORT_TYPES);
        add(7, ID_TB_SORT_ACTIONS);
        add(4, ID_FILE_EXIT);
        tb.AddButtons(n, b);
        m_hWndToolBar = tb;
    }

    void UpdateMenuChecks()
    {
        HMENU m = GetMenu();
        if (!m) return;
        auto chk = [&](UINT id, bool on) { ::CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
        chk(ID_VIEW_GRIDLINES, m_opt.showGridLines);
        chk(ID_VIEW_TOOLTIPS, m_opt.showInfoTip);
        chk(ID_OPT_DONTLOADICONS, m_opt.dontLoadIcons);
        chk(ID_OPT_DONTLOADHANG, m_opt.dontLoadHang);
        chk(ID_OPT_MARKDISABLED, m_opt.markDisabled);
        chk(ID_OPT_AUTODESKREFRESH, m_opt.autoDeskRefresh);
        chk(ID_OPT_SHOWURL, m_opt.showUrl);
        chk(ID_OPT_EXTRACTCAPTION, m_opt.extractCaption);
        chk(ID_OPT_SHOWAPPS, m_opt.showApps);
        chk(ID_OPT_HIDEDOT, m_opt.removeDot);
        chk(ID_OPT_ADDHEADER, m_opt.addHeaderLine);
        chk(ID_VIEW_MARKODDEVEN, m_opt.markOddEven);
        chk(ID_VIEW_BLACKBG, m_opt.blackBackground);
    }

    // ---- columns --------------------------------------------------------
    static void RebuildColumns(CListViewCtrl& lv, const ColumnDef* defs, const std::vector<int>& vis, const int* w)
    {
        while (lv.DeleteColumn(0)) {}
        int i = 0;
        for (int c : vis) lv.InsertColumn(i++, defs[c].title, defs[c].format, w[c]);
    }
    void ApplyGridLines()
    {
        for (CListViewCtrl* lv : { &m_types, &m_actions })
        {
            DWORD ex = lv->GetExtendedListViewStyle();
            ex = m_opt.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
            lv->SetExtendedListViewStyle(ex);
        }
    }

    // ---- cell text ------------------------------------------------------
    std::wstring TypeCell(int row, int col) const
    {
        std::wstring v = m_items[row].column(col);
        if (col == 0 && m_opt.removeDot && !v.empty() && v.front() == L'.') v.erase(0, 1);
        return v;
    }
    std::wstring ActionCell(int row, int col) const { return m_acts[row].column(col); }

    // ---- data -----------------------------------------------------------
    EnumOptions enumOpts() const
    {
        EnumOptions e;
        e.showUrlProtocols = m_opt.showUrl;
        e.showApplications = m_opt.showApps;
        e.loadVersionInfo = true;
        e.loadOwner = true;
        return e;
    }

    void Refresh()
    {
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        m_items = EnumerateFileTypes(enumOpts());
        SortTypes();
        m_types.SetItemCount((int)m_items.size());
        m_acts.clear();
        m_actions.SetItemCount(0);
        m_selectedType = -1;
        m_types.Invalidate();
        m_actions.Invalidate();
        UpdateStatus();
        ::SetCursor(prev);
        if (!m_startSelectExt.empty()) { SelectExt(m_startSelectExt); m_startSelectExt.clear(); }
    }

    void RefreshActions(int typeIndex)
    {
        m_selectedType = typeIndex;
        if (typeIndex < 0 || typeIndex >= (int)m_items.size()) { m_acts.clear(); }
        else m_acts = EnumerateActions(m_items[typeIndex], m_opt.extractCaption, true);
        SortActions();
        m_actions.SetItemCount((int)m_acts.size());
        m_actions.Invalidate();
        UpdateStatus();
    }

    void SortTypes()
    {
        const int col = m_typeSort; const bool asc = m_typeAsc;
        std::stable_sort(m_items.begin(), m_items.end(), [&](const FileType& a, const FileType& b) {
            int c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }
    void SortActions()
    {
        const int col = m_actSort; const bool asc = m_actAsc;
        std::stable_sort(m_acts.begin(), m_acts.end(), [&](const FileAction& a, const FileAction& b) {
            int c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    void SelectExt(const std::wstring& ext)
    {
        for (int i = 0; i < (int)m_items.size(); ++i)
            if (_wcsicmp(m_items[i].key.c_str(), ext.c_str()) == 0)
            {
                m_types.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                m_types.EnsureVisible(i, FALSE);
                RefreshActions(i);
                return;
            }
    }

    // ---- notifications --------------------------------------------------
    LRESULT OnTypesDispInfo(int, LPNMHDR p, BOOL&) { return FeedDispInfo(p, true); }
    LRESULT OnActionsDispInfo(int, LPNMHDR p, BOOL&) { return FeedDispInfo(p, false); }

    LRESULT FeedDispInfo(LPNMHDR p, bool types)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        int item = di->item.iItem;
        const auto& vis = types ? m_typeVis : m_actVis;
        int rows = types ? (int)m_items.size() : (int)m_acts.size();
        if (item < 0 || item >= rows) return 0;
        if (di->item.iSubItem < 0 || di->item.iSubItem >= (int)vis.size()) return 0;
        int col = vis[di->item.iSubItem];
        if (di->item.mask & LVIF_TEXT)
        {
            static std::wstring text;
            text = types ? TypeCell(item, col) : ActionCell(item, col);
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
        if ((di->item.mask & LVIF_IMAGE) && di->item.iSubItem == 0) di->item.iImage = 0;
        return 0;
    }

    LRESULT OnTypesChanged(int, LPNMHDR p, BOOL&)
    {
        auto* nm = reinterpret_cast<LPNMLISTVIEW>(p);
        if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_FOCUSED))
            RefreshActions(nm->iItem);
        else UpdateStatus();
        return 0;
    }
    LRESULT OnTypesFocus(int, LPNMHDR, BOOL&) { m_lastFocus = IDC_LIST_TYPES; return 0; }
    LRESULT OnActionsFocus(int, LPNMHDR, BOOL&) { m_lastFocus = IDC_LIST_ACTIONS; return 0; }

    LRESULT OnTypesColumnClick(int, LPNMHDR p, BOOL&)
    {
        int c = m_typeVis[reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem];
        if (c == m_typeSort) m_typeAsc = !m_typeAsc; else { m_typeSort = c; m_typeAsc = true; }
        SortTypes(); m_types.Invalidate();
        return 0;
    }
    LRESULT OnActionsColumnClick(int, LPNMHDR p, BOOL&)
    {
        int c = m_actVis[reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem];
        if (c == m_actSort) m_actAsc = !m_actAsc; else { m_actSort = c; m_actAsc = true; }
        SortActions(); m_actions.Invalidate();
        return 0;
    }

    LRESULT OnTypesDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnProperties(0, 0, nullptr, b); return 0; }

    LRESULT OnTypesRClick(int, LPNMHDR, BOOL&) { ShowContext(IDR_CONTEXT_TYPES); return 0; }
    LRESULT OnActionsRClick(int, LPNMHDR, BOOL&) { ShowContext(IDR_CONTEXT_ACTIONS); return 0; }
    void ShowContext(UINT menuId)
    {
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(menuId));
        if (!menu) return;
        HMENU sub = ::GetSubMenu(menu, 0);
        POINT pt; ::GetCursorPos(&pt);
        ::TrackPopupMenu(sub, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
        ::DestroyMenu(menu);
    }

    void UpdateStatus()
    {
        wchar_t buf[128];
        int sel = m_types.GetSelectedCount();
        if (sel > 0) wsprintfW(buf, L"%d File Types, %d Selected", (int)m_items.size(), sel);
        else         wsprintfW(buf, L"%d File Types", (int)m_items.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
        wsprintfW(buf, L"%d Actions", (int)m_acts.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)buf);
    }

    // ---- focused pane helpers -------------------------------------------
    bool typesFocused() const { return m_lastFocus == IDC_LIST_TYPES; }
    CListViewCtrl& activeList() { return typesFocused() ? m_types : m_actions; }

    std::vector<int> SelectedIndices(CListViewCtrl& lv)
    {
        std::vector<int> v;
        for (int i = lv.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = lv.GetNextItem(i, LVNI_SELECTED)) v.push_back(i);
        return v;
    }
    int FocusedIndex(CListViewCtrl& lv) { return lv.GetNextItem(-1, LVNI_FOCUSED); }

    ExportRequest TypesRequest(bool selectedOnly)
    {
        ExportRequest r;
        r.colDefs = kTypeColumns;
        r.columns = m_typeVis;
        r.reportTitle = L"File Types List";
        r.headerLine = m_opt.addHeaderLine;
        if (selectedOnly)
        {
            auto sel = SelectedIndices(m_types);
            auto rows = std::make_shared<std::vector<int>>(sel);
            r.rowCount = (int)rows->size();
            r.cell = [this, rows](int row, int col) { return TypeCell((*rows)[row], col); };
        }
        else
        {
            r.rowCount = (int)m_items.size();
            r.cell = [this](int row, int col) { return TypeCell(row, col); };
        }
        return r;
    }
    ExportRequest ActionsRequest(bool selectedOnly)
    {
        ExportRequest r;
        r.colDefs = kActionColumns;
        r.columns = m_actVis;
        r.reportTitle = L"File Type Actions";
        r.headerLine = m_opt.addHeaderLine;
        if (selectedOnly)
        {
            auto sel = SelectedIndices(m_actions);
            auto rows = std::make_shared<std::vector<int>>(sel);
            r.rowCount = (int)rows->size();
            r.cell = [this, rows](int row, int col) { return ActionCell((*rows)[row], col); };
        }
        else
        {
            r.rowCount = (int)m_acts.size();
            r.cell = [this](int row, int col) { return ActionCell(row, col); };
        }
        return r;
    }

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { activeList().SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED); UpdateStatus(); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { activeList().SetItemState(-1, 0, LVIS_SELECTED); UpdateStatus(); return 0; }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_opt.showGridLines = !m_opt.showGridLines; ApplyGridLines(); UpdateMenuChecks(); return 0; }

    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&)
    {
        CListViewCtrl& lv = activeList();
        int n = lv.GetHeader().GetItemCount();
        for (int i = 0; i < n; ++i) lv.SetColumnWidth(i, LVSCW_AUTOSIZE);
        return 0;
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        ExportRequest r = typesFocused() ? TypesRequest(true) : ActionsRequest(true);
        if (r.rowCount == 0) r = typesFocused() ? TypesRequest(false) : ActionsRequest(false);
        CopyToClipboard(BuildReport(r, ExportFormat::Tab));
        return 0;
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        bool types = typesFocused();
        ExportRequest sel = types ? TypesRequest(true) : ActionsRequest(true);
        ExportRequest req = sel.rowCount ? sel : (types ? TypesRequest(false) : ActionsRequest(false));

        CFileDialog dlg(FALSE, L"txt", types ? L"File Types.txt" : L"Actions.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
            L"Text File (*.txt)\0*.txt\0Tab delimited text file (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.htm;*.html)\0*.htm;*.html\0"
            L"HTML File - Vertical (*.htm;*.html)\0*.htm;*.html\0XML File (*.xml)\0*.xml\0"
            L"Comma delimited text file (*.csv)\0*.csv\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        dlg.m_ofn.nFilterIndex = m_opt.saveFilterIndex;
        if (dlg.DoModal() != IDOK) return 0;
        m_opt.saveFilterIndex = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex : 1;
        WriteReport(dlg.m_szFileName, req, FormatFromFilterIndex(m_opt.saveFilterIndex));
        return 0;
    }

    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(typesFocused() ? TypesRequest(false) : ActionsRequest(false)); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&)
    {
        ExportRequest r = typesFocused() ? TypesRequest(true) : ActionsRequest(true);
        if (r.rowCount == 0) r = typesFocused() ? TypesRequest(false) : ActionsRequest(false);
        ShowHtml(r);
        return 0;
    }
    void ShowHtml(const ExportRequest& r)
    {
        wchar_t tmp[MAX_PATH], dir[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"ftm", 0, tmp);
        std::wstring html = tmp; html += L".html";
        WriteReport(html, r, ExportFormat::HtmlHorizontal);
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        bool types = typesFocused();
        CColumnsDlg dlg(types ? kTypeColumns : kActionColumns,
                        types ? kTypeColumnCount : kActionColumnCount,
                        types ? m_typeVis : m_actVis);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            if (types) { m_typeVis = dlg.m_visible; RebuildColumns(m_types, kTypeColumns, m_typeVis, m_typeW); m_types.Invalidate(); }
            else { m_actVis = dlg.m_visible; RebuildColumns(m_actions, kActionColumns, m_actVis, m_actW); m_actions.Invalidate(); }
        }
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        std::vector<std::pair<std::wstring, std::wstring>> fields;
        if (typesFocused())
        {
            int i = FocusedIndex(m_types);
            if (i < 0 || i >= (int)m_items.size()) return 0;
            for (int c : m_typeVis) fields.emplace_back(kTypeColumns[c].title, TypeCell(i, c));
        }
        else
        {
            int i = FocusedIndex(m_actions);
            if (i < 0 || i >= (int)m_acts.size()) return 0;
            for (int c : m_actVis) fields.emplace_back(kActionColumns[c].title, ActionCell(i, c));
        }
        CPropertiesDlg(std::move(fields)).DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnTypeRegEdit(WORD, WORD, HWND, BOOL&)
    {
        int i = FocusedIndex(m_types);
        if (i >= 0 && i < (int)m_items.size()) OpenRegEdit(m_items[i].registryKey());
        return 0;
    }
    LRESULT OnActionRegEdit(WORD, WORD, HWND, BOOL&)
    {
        int i = FocusedIndex(m_actions);
        if (i >= 0 && i < (int)m_acts.size() && !m_acts[i].registryKey.empty())
            OpenRegEdit(m_acts[i].registryKey);
        return 0;
    }
    static void OpenRegEdit(const std::wstring& key)
    {
        HKEY k;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit",
                0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS)
        {
            std::wstring last = L"Computer\\" + key;
            ::RegSetValueExW(k, L"LastKey", 0, REG_SZ, (const BYTE*)last.c_str(),
                             (DWORD)((last.size() + 1) * sizeof(wchar_t)));
            ::RegCloseKey(k);
        }
        ::ShellExecuteW(nullptr, L"open", L"regedit.exe", nullptr, nullptr, SW_SHOWNORMAL);
    }

    // ---- writes (manage associations) -----------------------------------
    LRESULT OnDeleteExt(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedIndices(m_types);
        if (sel.empty()) return 0;
        if (::MessageBoxW(m_hWnd, L"Are you sure you want to delete the selected extensions ?",
                          L"FileTypesMan", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        for (int i : sel)
        {
            const FileType& t = m_items[i];
            if (!t.key.empty() && t.key.front() == L'.')
                ::RegDeleteTreeW(HKEY_CLASSES_ROOT, t.key.c_str());
        }
        Refresh();
        return 0;
    }

    LRESULT OnNewExt(WORD, WORD, HWND, BOOL&)
    {
        wchar_t ext[64] = L".";
        // A tiny inline prompt reusing the Properties template is overkill here, so the
        // new extension is created from the typed text of a simple input box.
        if (!PromptText(L"Type the new file extension (for example .abc):", ext, 64)) return 0;
        std::wstring e = ext;
        if (e.empty()) return 0;
        if (e.front() != L'.') e = L"." + e;
        HKEY k;
        if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, e.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS)
            ::RegCloseKey(k);
        Refresh();
        SelectExt(e);
        return 0;
    }

    LRESULT OnDeleteActions(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedIndices(m_actions);
        if (sel.empty()) return 0;
        if (::MessageBoxW(m_hWnd, L"Do you want to delete the selected actions ?",
                          L"FileTypesMan", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        for (int i : sel)
            if (!m_acts[i].regSubKey.empty())
                ::RegDeleteTreeW(HKEY_CLASSES_ROOT, m_acts[i].regSubKey.c_str());
        if (m_selectedType >= 0) RefreshActions(m_selectedType);
        return 0;
    }

    // Minimal modal text prompt, used by New File Extension.
    bool PromptText(const wchar_t* prompt, wchar_t* buf, int cch)
    {
        CInputDlg dlg(prompt, buf);
        if (dlg.DoModal(m_hWnd) != IDOK) return false;
        lstrcpynW(buf, dlg.m_text.c_str(), cch);
        return true;
    }

    LRESULT OnWebPage(WORD, WORD, HWND, BOOL&)
    {
        ::ShellExecuteW(m_hWnd, L"open", L"https://github.com/0xntpower/WinSysTools",
                        nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    // ---- options toggles ------------------------------------------------
    LRESULT OnToggleTooltips(WORD, WORD, HWND, BOOL&) { m_opt.showInfoTip = !m_opt.showInfoTip; UpdateMenuChecks(); return 0; }
    LRESULT OnToggleDontLoadIcons(WORD, WORD, HWND, BOOL&) { m_opt.dontLoadIcons = !m_opt.dontLoadIcons; UpdateMenuChecks(); return 0; }
    LRESULT OnToggleDontLoadHang(WORD, WORD, HWND, BOOL&) { m_opt.dontLoadHang = !m_opt.dontLoadHang; UpdateMenuChecks(); return 0; }
    LRESULT OnToggleMarkDisabled(WORD, WORD, HWND, BOOL&) { m_opt.markDisabled = !m_opt.markDisabled; UpdateMenuChecks(); m_actions.Invalidate(); return 0; }
    LRESULT OnToggleAutoDesk(WORD, WORD, HWND, BOOL&) { m_opt.autoDeskRefresh = !m_opt.autoDeskRefresh; UpdateMenuChecks(); return 0; }
    LRESULT OnToggleShowUrl(WORD, WORD, HWND, BOOL&) { m_opt.showUrl = !m_opt.showUrl; UpdateMenuChecks(); Refresh(); return 0; }
    LRESULT OnToggleExtractCaption(WORD, WORD, HWND, BOOL&) { m_opt.extractCaption = !m_opt.extractCaption; UpdateMenuChecks(); if (m_selectedType >= 0) RefreshActions(m_selectedType); return 0; }
    LRESULT OnToggleShowApps(WORD, WORD, HWND, BOOL&) { m_opt.showApps = !m_opt.showApps; UpdateMenuChecks(); Refresh(); return 0; }
    LRESULT OnToggleHideDot(WORD, WORD, HWND, BOOL&) { m_opt.removeDot = !m_opt.removeDot; UpdateMenuChecks(); m_types.Invalidate(); return 0; }
    LRESULT OnToggleAddHeader(WORD, WORD, HWND, BOOL&) { m_opt.addHeaderLine = !m_opt.addHeaderLine; UpdateMenuChecks(); return 0; }
    LRESULT OnToggleMarkOddEven(WORD, WORD, HWND, BOOL&) { m_opt.markOddEven = !m_opt.markOddEven; UpdateMenuChecks(); Invalidate(); return 0; }
    LRESULT OnToggleBlackBg(WORD, WORD, HWND, BOOL&) { m_opt.blackBackground = !m_opt.blackBackground; UpdateMenuChecks(); ApplyColors(); return 0; }

    void ApplyColors()
    {
        COLORREF bg = m_opt.blackBackground ? RGB(0, 0, 0) : ::GetSysColor(COLOR_WINDOW);
        COLORREF fg = m_opt.blackBackground ? RGB(255, 255, 255) : ::GetSysColor(COLOR_WINDOWTEXT);
        for (CListViewCtrl* lv : { &m_types, &m_actions })
        {
            lv->SetBkColor(bg); lv->SetTextBkColor(bg); lv->SetTextColor(fg); lv->Invalidate();
        }
    }

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        if (m_pFind) { m_pFind->SetFocus(); return 0; }
        m_pFind = new CFindReplaceDialog();
        m_pFind->Create(TRUE, L"", nullptr, FR_DOWN, m_hWnd);
        return 0;
    }
    LRESULT OnFindReplace(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
        if (fr->Flags & FR_DIALOGTERM) { m_pFind = nullptr; return 0; }
        if (fr->Flags & FR_FINDNEXT)   FindNext(fr->lpstrFindWhat, (fr->Flags & FR_DOWN) != 0);
        return 0;
    }
    void FindNext(const std::wstring& needle, bool down)
    {
        if (needle.empty()) return;
        bool types = typesFocused();
        CListViewCtrl& lv = types ? m_types : m_actions;
        int n = types ? (int)m_items.size() : (int)m_acts.size();
        const auto& vis = types ? m_typeVis : m_actVis;
        if (n == 0) return;
        std::wstring low = needle; std::transform(low.begin(), low.end(), low.begin(), towlower);
        int start = FocusedIndex(lv);
        for (int step = 1; step <= n; ++step)
        {
            int i = down ? (start + step) % n : (start - step + n) % n;
            std::wstring row;
            for (int c : vis) row += (types ? TypeCell(i, c) : ActionCell(i, c)) + L" ";
            std::transform(row.begin(), row.end(), row.begin(), towlower);
            if (row.find(low) != std::wstring::npos)
            {
                lv.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                lv.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                lv.EnsureVisible(i, FALSE);
                if (types) RefreshActions(i);
                return;
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

    // ---- config (.cfg INI [General]) ------------------------------------
    int iniInt(const wchar_t* key, int def) { return (int)::GetPrivateProfileIntW(L"General", key, def, m_cfgPath.c_str()); }
    void iniSetInt(const wchar_t* key, int v) { wchar_t b[16]; wsprintfW(b, L"%d", v); ::WritePrivateProfileStringW(L"General", key, b, m_cfgPath.c_str()); }
    std::wstring iniStr(const wchar_t* key)
    {
        wchar_t buf[1024] = {};
        ::GetPrivateProfileStringW(L"General", key, L"", buf, 1024, m_cfgPath.c_str());
        return buf;
    }

    void LoadConfig()
    {
        if (m_noSaveLoad) return;
        m_opt.showGridLines   = iniInt(L"ShowGridLines", 0) != 0;
        m_opt.saveFilterIndex = std::clamp(iniInt(L"SaveFilterIndex", 1), 1, 7);
        m_opt.showInfoTip     = iniInt(L"ShowInfoTip", 1) != 0;
        m_opt.dontLoadIcons   = iniInt(L"DontLoadIcons", 0) != 0;
        m_opt.blackBackground = iniInt(L"BlackBackground", 0) != 0;
        m_opt.dontLoadHang    = iniInt(L"DontLoadHangIcons", 1) != 0;
        m_opt.markDisabled    = iniInt(L"MarkDisabledActions", 1) != 0;
        m_opt.autoDeskRefresh = iniInt(L"AutoDesktopRefresh", 1) != 0;
        m_opt.showUrl         = iniInt(L"ShowURLProtocols", 0) != 0;
        m_opt.extractCaption  = iniInt(L"ExtractResourceCaption", 1) != 0;
        m_opt.showApps        = iniInt(L"ShowApplicationsTypes", 1) != 0;
        m_opt.removeDot       = iniInt(L"RemoveExtensionDot", 0) != 0;
        m_opt.addHeaderLine   = iniInt(L"AddExportHeaderLine", 0) != 0;
        m_opt.markOddEven     = iniInt(L"MarkOddEvenRows", 0) != 0;
        m_typeSort = std::clamp(iniInt(L"Sort", 0), 0, kTypeColumnCount - 1);
        m_actSort  = std::clamp(iniInt(L"ActionsSort", 0), 0, kActionColumnCount - 1);
        if (m_startSelectExt.empty()) m_startSelectExt = iniStr(L"SelectedExt");

        auto wp = parseHex(iniStr(L"WinPos"));
        if (wp.size() >= sizeof(WINDOWPLACEMENT))
        {
            WINDOWPLACEMENT p{}; memcpy(&p, wp.data(), sizeof(p)); p.length = sizeof(p);
            SetWindowPlacement(&p);
        }
        LoadColumns(iniStr(L"Columns"), m_typeVis, m_typeW, kTypeColumnCount);
        LoadColumns(iniStr(L"ActionsColumns"), m_actVis, m_actW, kActionColumnCount);
    }

    void SaveConfig()
    {
        if (m_noSaveLoad) return;
        iniSetInt(L"ShowGridLines", m_opt.showGridLines);
        iniSetInt(L"SaveFilterIndex", m_opt.saveFilterIndex);
        iniSetInt(L"ShowInfoTip", m_opt.showInfoTip);
        iniSetInt(L"DontLoadIcons", m_opt.dontLoadIcons);
        iniSetInt(L"BlackBackground", m_opt.blackBackground);
        iniSetInt(L"DontLoadHangIcons", m_opt.dontLoadHang);
        iniSetInt(L"MarkDisabledActions", m_opt.markDisabled);
        iniSetInt(L"AutoDesktopRefresh", m_opt.autoDeskRefresh);
        iniSetInt(L"ShowURLProtocols", m_opt.showUrl);
        iniSetInt(L"ExtractResourceCaption", m_opt.extractCaption);
        iniSetInt(L"ShowApplicationsTypes", m_opt.showApps);
        iniSetInt(L"RemoveExtensionDot", m_opt.removeDot);
        iniSetInt(L"AddExportHeaderLine", m_opt.addHeaderLine);
        iniSetInt(L"MarkOddEvenRows", m_opt.markOddEven);
        iniSetInt(L"Sort", m_typeSort);
        iniSetInt(L"ActionsSort", m_actSort);
        int sel = FocusedIndex(m_types);
        if (sel >= 0 && sel < (int)m_items.size())
            ::WritePrivateProfileStringW(L"General", L"SelectedExt", m_items[sel].key.c_str(), m_cfgPath.c_str());

        WINDOWPLACEMENT p{ sizeof(p) };
        GetWindowPlacement(&p);
        ::WritePrivateProfileStringW(L"General", L"WinPos", hexBytes(&p, sizeof(p)).c_str(), m_cfgPath.c_str());
        SaveColumns(L"Columns", m_types, m_typeVis, m_typeW);
        SaveColumns(L"ActionsColumns", m_actions, m_actVis, m_actW);
    }

    void LoadColumns(const std::wstring& blob, std::vector<int>& vis, int* w, int count)
    {
        auto cols = parseHex(blob);
        if (cols.size() < 4) return;
        std::vector<int> v;
        for (size_t i = 0; i + 4 <= cols.size(); i += 4)
        {
            int width = cols[i] | (cols[i + 1] << 8);
            int idx   = cols[i + 2] | (cols[i + 3] << 8);
            if (idx >= 0 && idx < count) { v.push_back(idx); if (width) w[idx] = width; }
        }
        if (!v.empty()) vis = v;
    }
    void SaveColumns(const wchar_t* key, CListViewCtrl& lv, std::vector<int>& vis, int* w)
    {
        std::vector<BYTE> blob;
        int headerN = lv.IsWindow() ? lv.GetHeader().GetItemCount() : 0;
        for (size_t i = 0; i < vis.size(); ++i)
        {
            int c = vis[i];
            int width = (int)i < headerN ? lv.GetColumnWidth((int)i) : w[c];
            blob.push_back(width & 0xFF); blob.push_back((width >> 8) & 0xFF);
            blob.push_back(c & 0xFF);     blob.push_back((c >> 8) & 0xFF);
        }
        ::WritePrivateProfileStringW(L"General", key, hexBytes(blob.data(), blob.size()).c_str(), m_cfgPath.c_str());
    }

    static std::vector<BYTE> parseHex(const std::wstring& s)
    {
        std::vector<BYTE> out;
        for (size_t i = 0; i < s.size(); )
        {
            while (i < s.size() && s[i] == L' ') ++i;
            if (i >= s.size()) break;
            out.push_back((BYTE)wcstoul(s.substr(i, 2).c_str(), nullptr, 16));
            while (i < s.size() && s[i] != L' ') ++i;
        }
        return out;
    }
    static std::wstring hexBytes(const void* data, size_t n)
    {
        const BYTE* p = (const BYTE*)data;
        std::wstring s; wchar_t b[4];
        for (size_t i = 0; i < n; ++i) { wsprintfW(b, i ? L" %02X" : L"%02X", p[i]); s += b; }
        return s;
    }

    void OnDestroy()
    {
        SaveConfig();
        if (CMessageLoop* pLoop = _Module.GetMessageLoop()) pLoop->RemoveMessageFilter(this);
        PostQuitMessage(0);
    }
};
