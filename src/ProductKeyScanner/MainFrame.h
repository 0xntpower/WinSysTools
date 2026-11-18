// MainFrame.h -- WTL main window for the ProductKeyScanner recreation.
// Visual layout (menu 102, toolbar bitmap 110, status bar, report ListView) matches
// the original resources. Logic is clean C++20 over the EnumerateProductKeys() /
// DecodeProductKeyBlob() model rather than the framework code. The scan runs on a
// worker thread so the UI stays responsive and Esc can cancel it (v1.02 behaviour).
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
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <memory>
#include "resource.h"
#include "../common/UiUtil.h"
#include "../common/CfgIni.h"
#include "ProductKey.h"
#include "Columns.h"
#include "Export.h"
#include "RegKey.h"
#include "Dialogs.h"
#include "Scanner.h"

// Legacy registry root the /deleteregkey switch removes. Live settings are kept in
// the .cfg file next to the executable (see LoadConfig/SaveConfig), not here.
constexpr wchar_t kConfigKey[] = L"Software\\0xntpower\\ProductKeyScanner";
constexpr UINT WM_APP_SCANDONE = WM_APP + 1;

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"ProductKeyScanner", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    CImageList m_images;
    std::vector<ProductKeyRecord> m_items;
    std::vector<int> m_visible;
    std::vector<int> m_widths; // per visible column, display order (Columns blob)
    std::wstring m_cfgPath = cfgini::DefaultPath();
    ScanOptions m_opts;
    int m_sortCol = 0; // default sort on Product ID (matches the original)
    bool m_sortAsc = true;
    bool m_gridLines = true;
    bool m_showInfoTip = true;  // ShowInfoTip
    bool m_markOddEven = false; // MarkOddEvenRows
    bool m_addHeader = false;   // AddExportHeaderLine
    bool m_noSaveLoad = false;  // /nosaveload
    int m_saveFilterIndex = 1;  // SaveFilterIndex
    int m_startShowCmd = SW_SHOWNORMAL;
    HACCEL m_hAccel = nullptr;
    HFONT m_font = nullptr;
    LOGFONTW m_logFont{};
    bool m_haveFont = false;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_scanning{false};
    HANDLE m_thread = nullptr;

    CMainFrame();

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MESSAGE_HANDLER(WM_APP_SCANDONE, OnScanDone)
    MESSAGE_HANDLER(m_findMsg, OnFindReplace)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
    NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
    NOTIFY_HANDLER(IDC_LISTVIEW, NM_CUSTOMDRAW, OnCustomDraw)
    COMMAND_ID_HANDLER(ID_FILE_SCANOPTIONS, OnScanOptions)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_STOP, OnStop)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_EDIT_COPYKEY, OnCopyKey)
    COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_EDIT_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_VIEW_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_VIEW_TOOLTIPS, OnTooltips)
    COMMAND_ID_HANDLER(ID_VIEW_MARKODDEVEN, OnMarkOddEven)
    COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE_HDR, OnAutoSizeHdr)
    COMMAND_ID_HANDLER(ID_VIEW_COLUMNS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_VIEW_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_VIEW_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_OPTIONS_ADDHEADER, OnAddHeader)
    COMMAND_ID_HANDLER(ID_OPTIONS_FONT, OnSelectFont)
    COMMAND_ID_HANDLER(ID_OPTIONS_DEFFONT, OnDefaultFont)
    COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()
    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void UpdateMenuChecks();

    // ---- columns --------------------------------------------------------
    void RebuildColumns();
    void ApplyGridLines();
    void ApplyFont();

    // ---- scanning (worker thread) --------------------------------------
    struct ScanArg
    {
        CMainFrame* self;
        ScanOptions opts;
    };

    static DWORD WINAPI ScanProc(LPVOID p);
    void StartScan();
    LRESULT OnScanDone(UINT, WPARAM, LPARAM lParam, BOOL&);
    void WaitThread();

    // ---- sorting --------------------------------------------------------
    void SortItems();
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
    LRESULT OnCustomDraw(int, LPNMHDR p, BOOL&);

    // ---- status ---------------------------------------------------------
    void UpdateStatus();

    // ---- selection helpers ---------------------------------------------
    std::vector<int> SelectedIndices();
    std::vector<ProductKeyRecord> SelectedItems();

    // ---- commands -------------------------------------------------------
    LRESULT OnScanOptions(WORD, WORD, HWND, BOOL&);
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnStop(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&);
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnAddHeader(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectFont(WORD, WORD, HWND, BOOL&);
    LRESULT OnDefaultFont(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyKey(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    void ShowHtmlReport(const std::vector<ProductKeyRecord>& items);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindReplace(UINT, WPARAM, LPARAM lParam, BOOL&);
    void FindNext(const std::wstring& needle, bool down);
    LRESULT OnDblClk(int, LPNMHDR, BOOL&);
    LRESULT OnRClick(int, LPNMHDR, BOOL&);
    void CopyToClipboard(const std::wstring& text);

    // ---- config ---------------------------------------------------------
    void SetCfgPath(const std::wstring& arg);
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
