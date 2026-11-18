// MainFrame.h -- WTL main window for the IconsExtract recreation.
// Layout (menu 102, 6-button toolbar from bitmap 104, status bar, icon ListView
// with Large/Small/List/Details views) matches sub_4051F2. The scan runs over the
// clean IconScanner model. Config lives in iconsext.cfg (see Config.h).
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
#include <vector>
#include <string>
#include "resource.h"
#include "../common/UiUtil.h"
#include "IconEntry.h"
#include "Columns.h"
#include "ResourceScan.h"
#include "IconSave.h"
#include "Config.h"
#include "Dialogs.h"

extern CAppModule _Module;

constexpr UINT WM_APP_STARTUP = WM_USER + 4;   // original PostMessage(0x404)

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"IconsExtract", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    CImageList    m_large;
    CImageList    m_small;
    std::vector<IconEntry> m_items;
    AppConfig     m_cfg;
    DWORD         m_pid = 0;
    HACCEL        m_hAccel = nullptr;
    int           m_startShowCmd = SW_SHOWNORMAL;
    bool          m_showSearchOnStartup = true;
    bool          m_noLoadSettings = false;
    std::wstring  m_scanPathOverride;         // -scanpath value, applied after config load
    volatile bool m_stop = false;

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MESSAGE_HANDLER(WM_APP_STARTUP, OnStartup)
        MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
        COMMAND_ID_HANDLER(ID_FILE_SEARCH, OnSearch)
        COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_FILE_FILEPROPS, OnFileProperties)
        COMMAND_ID_HANDLER(ID_VIEW_LARGE, OnViewLarge)
        COMMAND_ID_HANDLER(ID_VIEW_SMALL, OnViewSmall)
        COMMAND_ID_HANDLER(ID_VIEW_LIST, OnViewList)
        COMMAND_ID_HANDLER(ID_VIEW_DETAILS, OnViewDetails)
        COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()
    LRESULT OnEraseBkgnd(CDCHandle dc) { return FillFrameBtnFace(m_hWnd, dc); }

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        CreateToolbar();
        m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                                L"", m_hWnd, IDC_STATUSBAR);

        m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
            WS_EX_CLIENTEDGE, IDC_LISTVIEW);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);

        m_large.Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 16);
        m_small.Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 16);
        m_list.SetImageList(m_large, LVSIL_NORMAL);
        m_list.SetImageList(m_small, LVSIL_SMALL);

        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        if (!m_noLoadSettings) m_cfg.Load();
        if (!m_scanPathOverride.empty()) m_cfg.wildcard = m_scanPathOverride;
        BuildColumns();
        ApplyViewMode(m_cfg.lvMode);
        ::DragAcceptFiles(m_hWnd, TRUE);

        if (m_cfg.winPos.size() >= sizeof(WINDOWPLACEMENT))
        {
            WINDOWPLACEMENT p{};
            memcpy(&p, m_cfg.winPos.data(), sizeof(p));
            p.length = sizeof(p);
            SetWindowPlacement(&p);
            m_startShowCmd = p.showCmd;
        }
        else
        {
            // No saved placement: the CreateEx rect already set the original's 640 x 480
            // outer window, so only center it, as the original does on first launch.
            CenterWindow();
        }

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
            pLoop->AddMessageFilter(this);

        SetWindowTextW(L"IconsExtract");
        PostMessage(WM_APP_STARTUP);
        return 0;
    }

    void CreateToolbar()
    {
        CToolBarCtrl tb;
        tb.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
                  0, IDC_TOOLBAR);
        tb.SetButtonStructSize(sizeof(TBBUTTON));
        PinToolbarLight(tb);
        tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
        TBBUTTON b[6] = {};
        auto set = [&](int i, int bmp, int cmd, BYTE style) {
            b[i].iBitmap = bmp; b[i].idCommand = cmd; b[i].fsState = TBSTATE_ENABLED; b[i].fsStyle = style;
        };
        set(0, 3, ID_FILE_SEARCH, TBSTYLE_BUTTON);
        set(1, 0, ID_FILE_SAVE, TBSTYLE_BUTTON);
        set(2, 1, ID_EDIT_COPY, TBSTYLE_BUTTON);
        set(3, 2, ID_FILE_PROPERTIES, TBSTYLE_BUTTON);
        set(4, 0, 0, TBSTYLE_SEP);
        set(5, 4, ID_FILE_EXIT, TBSTYLE_BUTTON);
        tb.AddButtons(6, b);
        m_hWndToolBar = tb;
    }

    void BuildColumns()
    {
        for (int c = 0; c < kColumnCount; ++c)
            m_list.InsertColumn(c, kColumns[c].title, kColumns[c].format, m_cfg.columnWidths[c]);
    }

    // ---- view modes -----------------------------------------------------
    void ApplyViewMode(int mode)
    {
        static const DWORD styles[] = { LVS_ICON, LVS_SMALLICON, LVS_LIST, LVS_REPORT };
        if (mode < 0 || mode > 3) mode = 0;
        m_cfg.lvMode = mode;
        m_list.ModifyStyle(LVS_TYPEMASK, styles[mode]);
        CheckViewMenu(mode);
    }

    void CheckViewMenu(int mode)
    {
        static const int ids[] = { ID_VIEW_LARGE, ID_VIEW_SMALL, ID_VIEW_LIST, ID_VIEW_DETAILS };
        if (HMENU menu = GetMenu())
            ::CheckMenuRadioItem(menu, ID_VIEW_LARGE, ID_VIEW_DETAILS, ids[mode], MF_BYCOMMAND);
    }

    LRESULT OnViewLarge(WORD, WORD, HWND, BOOL&) { ApplyViewMode(0); return 0; }
    LRESULT OnViewSmall(WORD, WORD, HWND, BOOL&) { ApplyViewMode(1); return 0; }
    LRESULT OnViewList(WORD, WORD, HWND, BOOL&) { ApplyViewMode(2); return 0; }
    LRESULT OnViewDetails(WORD, WORD, HWND, BOOL&) { ApplyViewMode(3); return 0; }

    // ---- scanning -------------------------------------------------------
    LRESULT OnStartup(UINT, WPARAM, LPARAM, BOOL&)
    {
        if (m_showSearchOnStartup) DoSearch();
        return 0;
    }

    LRESULT OnSearch(WORD, WORD, HWND, BOOL&) { DoSearch(); return 0; }

    void DoSearch()
    {
        CSearchDlg dlg(m_cfg.wildcard, m_cfg.search, m_cfg.scanMode, m_pid);
        if (dlg.DoModal(m_hWnd) != IDOK) return;
        RunScan();
    }

    void RunScan()
    {
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        m_stop = false;
        m_list.DeleteAllItems();
        m_large.RemoveAll();
        m_small.RemoveAll();
        m_items.clear();

        IconScanner scanner;
        scanner.options = m_cfg.search;
        scanner.cancel = &m_stop;
        scanner.visit = [this](HMODULE hModule, IconEntry& e) {
            int idx = -1;
            if (HICON hl = ExtractGroupIcon(hModule, e.resourceId(), e.isCursor, 32, 32))
            {
                idx = m_large.AddIcon(hl);
                ::DestroyIcon(hl);
            }
            if (HICON hs = ExtractGroupIcon(hModule, e.resourceId(), e.isCursor, 16, 16))
            {
                int s = m_small.AddIcon(hs);
                if (idx < 0) idx = s;
                ::DestroyIcon(hs);
            }
            e.largeImage = e.smallImage = idx;
        };

        if (m_cfg.scanMode == 2 && m_pid)
            scanner.scanProcess(m_pid);
        else
            scanner.scanPath(m_cfg.wildcard);

        m_items = std::move(scanner.results);
        PopulateList();

        wchar_t title[1200];
        wsprintfW(title, L"IconsExtract:   %s", m_cfg.wildcard.c_str());
        SetWindowTextW(title);
        UpdateStatus();
        ::SetCursor(prev);
    }

    void PopulateList()
    {
        m_list.SetRedraw(FALSE);
        for (int i = 0; i < (int)m_items.size(); ++i)
        {
            const IconEntry& e = m_items[i];
            LVITEMW it{};
            it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            it.iItem = i;
            std::wstring col0 = e.column(0);
            it.pszText = const_cast<LPWSTR>(col0.c_str());
            it.iImage = e.largeImage;
            it.lParam = i;
            m_list.InsertItem(&it);
            for (int c = 1; c < kColumnCount; ++c)
                m_list.SetItemText(i, c, e.column(c).c_str());
        }
        m_list.SetRedraw(TRUE);
    }

    // ---- drag-drop ------------------------------------------------------
    LRESULT OnDropFiles(UINT, WPARAM wParam, LPARAM, BOOL&)
    {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH];
        if (::DragQueryFileW(drop, 0, path, MAX_PATH))
        {
            m_cfg.wildcard = path;
            RunScan();
        }
        ::DragFinish(drop);
        return 0;
    }

    // ---- selection helpers ---------------------------------------------
    std::vector<IconEntry> SelectedItems()
    {
        std::vector<IconEntry> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            if (i < (int)m_items.size()) v.push_back(m_items[i]);
        return v;
    }
    int FocusedIndex() { return m_list.GetNextItem(-1, LVNI_FOCUSED); }

    // ---- commands -------------------------------------------------------
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&)
    {
        m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
        UpdateStatus();
        return 0;
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) return 0;
        CSaveDlg dlg(m_cfg.saveFolder, m_cfg.saveCursorsAsIco);
        if (dlg.DoModal(m_hWnd) != IDOK || m_cfg.saveFolder.empty()) return 0;

        if (::GetFileAttributesW(m_cfg.saveFolder.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            if (MessageBox(L"The folder doesn't exist, do you want to create it ?",
                           L"IconsExtract", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            ::CreateDirectoryW(m_cfg.saveFolder.c_str(), nullptr);
        }
        SaveStats st = SaveEntries(m_cfg.saveFolder, sel, m_cfg.saveCursorsAsIco);
        wchar_t msg[128];
        wsprintfW(msg, L"%d icon(s) saved.", st.saved);
        MessageBox(msg, L"IconsExtract", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        int i = FocusedIndex();
        if (i < 0 || i >= (int)m_items.size()) return 0;
        HMODULE hModule = ::LoadLibraryExW(m_items[i].filename.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (!hModule) return 0;
        if (HICON hIcon = ExtractGroupIcon(hModule, m_items[i].resourceId(), m_items[i].isCursor, 32, 32))
        {
            ICONINFO ii{};
            if (::GetIconInfo(hIcon, &ii) && ::OpenClipboard(m_hWnd))
            {
                ::EmptyClipboard();
                ::SetClipboardData(CF_BITMAP, ii.hbmColor);
                ::CloseClipboard();
                if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
            }
            ::DestroyIcon(hIcon);
        }
        ::FreeLibrary(hModule);
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = FocusedIndex();
        if (i < 0 || i >= (int)m_items.size()) return 0;
        CPropertiesDlg dlg(m_items[i]);
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnFileProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = FocusedIndex();
        if (i < 0 || i >= (int)m_items.size()) return 0;
        SHELLEXECUTEINFOW ei{ sizeof(ei) };
        ei.fMask = SEE_MASK_INVOKEIDLIST;
        ei.hwnd = m_hWnd;
        ei.lpVerb = L"properties";
        ei.lpFile = m_items[i].filename.c_str();
        ei.nShow = SW_SHOW;
        ::ShellExecuteExW(&ei);
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    // ---- notifications --------------------------------------------------
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }
    LRESULT OnDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnProperties(0, 0, nullptr, b); return 0; }

    LRESULT OnRClick(int, LPNMHDR, BOOL&)
    {
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CONTEXT_MENU));
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
        if (sel > 0) wsprintfW(buf, L"%d item(s), %d Selected", (int)m_items.size(), sel);
        else         wsprintfW(buf, L"%d item(s)", (int)m_items.size());
        ::SetWindowTextW(m_hWndStatusBar, buf);
    }

    // ---- config / teardown ---------------------------------------------
    void SaveConfig()
    {
        WINDOWPLACEMENT p{ sizeof(p) };
        GetWindowPlacement(&p);
        m_cfg.winPos.assign(reinterpret_cast<BYTE*>(&p), reinterpret_cast<BYTE*>(&p) + sizeof(p));
        for (int c = 0; c < kColumnCount; ++c)
        {
            int w = m_list.GetColumnWidth(c);
            if (w > 0) m_cfg.columnWidths[c] = w;
        }
        m_cfg.Save();
    }

    void OnDestroy()
    {
        SaveConfig();
        PostQuitMessage(0);
    }
};
