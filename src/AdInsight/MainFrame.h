// MainFrame.h -- the AdInsight main window.
//
// A toolbar and status bar frame a report list of captured LDAP calls over a
// detail pane. Each row is one wldap32 client call reported by the injected
// hook DLL across the \\.\pipe\ADInsightPipe named pipe. The frame is pinned to
// light mode with the shared toolbar fix so the band reads light on a dark
// system, matching the original. Capture start drops and loads the hook DLL and
// opens the pipe server, exactly as the original drops its BINRES hook DLL.
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
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "TraceProtocol.h"
#include "Injector.h"
#include "Dialogs.h"

extern CAppModule _Module;

// Overlapped named-pipe server that receives TraceRecords from the hook DLL
// and posts each one to the window. One instance at a time, with a reconnect
// loop, matching the original's single-instance ADInsightPipe.
class PipeServer
{
  public:
    bool Start(HWND notify)
    {
        if (m_thread != nullptr)
        {
            return true;
        }
        m_notify = notify;
        m_stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_io = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (m_stop == nullptr || m_io == nullptr)
        {
            return false;
        }
        m_thread = ::CreateThread(nullptr, 0, &ThreadThunk, this, 0, nullptr);
        return m_thread != nullptr;
    }

    void Stop()
    {
        if (m_thread == nullptr)
        {
            return;
        }
        ::SetEvent(m_stop);
        ::WaitForSingleObject(m_thread, 3000);
        ::CloseHandle(m_thread);
        m_thread = nullptr;
        if (m_stop != nullptr)
        {
            ::CloseHandle(m_stop);
            m_stop = nullptr;
        }
        if (m_io != nullptr)
        {
            ::CloseHandle(m_io);
            m_io = nullptr;
        }
    }

    ~PipeServer()
    {
        Stop();
    }

  private:
    static DWORD WINAPI ThreadThunk(LPVOID p)
    {
        return static_cast<PipeServer*>(p)->Run();
    }

    DWORD Run()
    {
        for (;;)
        {
            HANDLE pipe = ::CreateNamedPipeW(adin::kPipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                             PIPE_UNLIMITED_INSTANCES, 0, sizeof(adin::TraceRecord) * 16, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                return 0;
            }
            if (!WaitConnect(pipe))
            {
                ::CloseHandle(pipe);
                return 0;
            }
            ReadLoop(pipe);
            ::DisconnectNamedPipe(pipe);
            ::CloseHandle(pipe);
            if (::WaitForSingleObject(m_stop, 0) == WAIT_OBJECT_0)
            {
                return 0;
            }
        }
    }

    // Wait for a client, cancellable through the stop event. Returns false when
    // asked to stop.
    bool WaitConnect(HANDLE pipe)
    {
        OVERLAPPED ov = {};
        ov.hEvent = m_io;
        ::ResetEvent(m_io);
        const BOOL pending = ::ConnectNamedPipe(pipe, &ov);
        if (pending || ::GetLastError() == ERROR_PIPE_CONNECTED)
        {
            return true;
        }
        if (::GetLastError() != ERROR_IO_PENDING)
        {
            return false;
        }
        HANDLE waits[2] = {m_stop, m_io};
        const DWORD w = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0 + 1)
        {
            ::CancelIo(pipe);
            return false;
        }
        return true;
    }

    void ReadLoop(HANDLE pipe)
    {
        for (;;)
        {
            adin::TraceRecord rec = {};
            OVERLAPPED ov = {};
            ov.hEvent = m_io;
            ::ResetEvent(m_io);
            DWORD got = 0;
            const BOOL ok = ::ReadFile(pipe, &rec, sizeof(rec), &got, &ov);
            if (!ok && ::GetLastError() == ERROR_IO_PENDING)
            {
                HANDLE waits[2] = {m_stop, m_io};
                const DWORD w = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w != WAIT_OBJECT_0 + 1)
                {
                    ::CancelIo(pipe);
                    return;
                }
                if (!::GetOverlappedResult(pipe, &ov, &got, FALSE))
                {
                    return;
                }
            }
            else if (!ok)
            {
                return;
            }
            if (got == sizeof(rec) && rec.magic == adin::kRecordMagic)
            {
                auto* copy = new adin::TraceRecord(rec);
                ::PostMessageW(m_notify, adin::kMsgRecord, 0, reinterpret_cast<LPARAM>(copy));
            }
        }
    }

    HWND m_notify = nullptr;
    HANDLE m_thread = nullptr;
    HANDLE m_stop = nullptr;
    HANDLE m_io = nullptr;
};

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"ADInsightWClass", IDR_MAINFRAME)

    CListViewCtrl m_list;
    CEdit m_detail;

    std::vector<adin::TraceRecord> m_rows;        // one per request, response merged in
    std::unordered_map<uint32_t, int> m_seqIndex; // request seq to list item

    bool m_capturing = false;
    bool m_startCapture = true; // original captures by default unless -o
    bool m_autoscroll = true;
    bool m_alwaysOnTop = false;
    int m_splitY = 0;
    bool m_dragging = false;
    static constexpr int kSplitBar = 5;
    static constexpr int kColCount = 9;

    PipeServer m_pipe;
    adin::HookDll m_hook;
    std::wstring m_dllPath;

    HACCEL m_hAccel = nullptr;
    WINDOWPLACEMENT m_placement = {};
    bool m_havePlacement = false;

    static constexpr const wchar_t* kCfgKey = L"Software\\Sysinternals\\ADInsight";

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel != nullptr && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
        {
            return TRUE;
        }
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETCURSOR(OnSetCursor)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    MESSAGE_HANDLER(adin::kMsgRecord, OnRecord)
    MESSAGE_HANDLER(adin::kMsgConnect, OnPipeConnect)
    MESSAGE_HANDLER(adin::kMsgError, OnPipeError)
    NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnListItemChanged)
    COMMAND_ID_HANDLER(ID_CAPTURE_EVENTS, OnCapture)
    COMMAND_ID_HANDLER(ID_CAPTURE_AUTOSCROLL, OnAutoscroll)
    COMMAND_ID_HANDLER(ID_EDIT_CLEAR, OnClear)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_FILE_SAVE_AS, OnSave)
    COMMAND_ID_HANDLER(ID_OPT_SELECTCOLS, OnSelectColumns)
    COMMAND_ID_HANDLER(ID_OPT_FONT, OnFont)
    COMMAND_ID_HANDLER(ID_OPT_ALWAYSONTOP, OnAlwaysOnTop)
    COMMAND_ID_HANDLER(ID_OPT_FILTER, OnFilter)
    COMMAND_ID_HANDLER(ID_INJECT_PID, OnInject)
    COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_HELP, OnHelp)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc)
    {
        return FillFrameBtnFace(m_hWnd, dc);
    }

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_MAINFRAME));
        LoadConfig();

        CreateToolbar();
        CreateSimpleStatusBar();

        m_list.Create(m_hWnd, rcDefault, nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, IDC_LIST);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
        BuildColumns();

        m_detail.Create(m_hWnd, rcDefault, nullptr,
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0,
                        IDC_DETAIL);
        m_detail.SetFont(AtlGetDefaultGuiFont());

        if (HICON hi = static_cast<HICON>(::LoadImageW(
                ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        SetWindowTextW(L"AdInsight - Sysinternals: www.sysinternals.com");

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
        {
            pLoop->AddMessageFilter(this);
        }
        if (m_havePlacement)
        {
            SetWindowPlacement(&m_placement);
        }
        if (m_alwaysOnTop)
        {
            SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
        if (m_startCapture)
        {
            StartCapture();
        }
        SyncMenu();
        UpdateStatus();
        return 0;
    }

    void CreateToolbar()
    {
        CToolBarCtrl tb;
        tb.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
                  0, IDC_TOOLBAR);
        tb.SetButtonStructSize(sizeof(TBBUTTON));
        PinToolbarLight(tb);

        CImageList il;
        il.Create(16, 16, ILC_COLOR32 | ILC_MASK, 5, 2);
        const SHSTOCKICONID stock[] = {SIID_DOCASSOC, SIID_STACK, SIID_DELETE, SIID_FIND, SIID_HELP};
        for (SHSTOCKICONID id : stock)
        {
            SHSTOCKICONINFO sii = {};
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(::SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon != nullptr)
            {
                il.AddIcon(sii.hIcon);
                ::DestroyIcon(sii.hIcon);
            }
        }
        tb.SetImageList(il.Detach());

        struct ButtonDef
        {
            int bmp;
            int cmd;
        };
        const ButtonDef defs[] = {
            {0, ID_CAPTURE_EVENTS}, {1, ID_CAPTURE_AUTOSCROLL}, {-1, 0}, {2, ID_EDIT_CLEAR}, {3, ID_EDIT_FIND}, {-1, 0},
            {4, ID_HELP},
        };
        std::vector<TBBUTTON> b;
        for (const ButtonDef& d : defs)
        {
            TBBUTTON t = {};
            if (d.bmp < 0)
            {
                t.fsStyle = TBSTYLE_SEP;
            }
            else
            {
                t.iBitmap = d.bmp;
                t.idCommand = d.cmd;
                t.fsState = TBSTATE_ENABLED;
                t.fsStyle = TBSTYLE_BUTTON;
            }
            b.push_back(t);
        }
        tb.AddButtons(static_cast<int>(b.size()), b.data());
        tb.AutoSize();
        m_hWndToolBar = tb;
    }

    void BuildColumns()
    {
        struct ColDef
        {
            const wchar_t* title;
            int width;
            int fmt;
        };
        // The original's main list columns, in order.
        const ColDef cols[kColCount] = {
            {L"ID", 50, LVCFMT_RIGHT},       {L"Process", 140, LVCFMT_LEFT},
            {L"Request", 140, LVCFMT_LEFT},  {L"Domain Controller", 120, LVCFMT_LEFT},
            {L"User", 150, LVCFMT_LEFT},     {L"Input", 240, LVCFMT_LEFT},
            {L"Output", 110, LVCFMT_LEFT},   {L"Result", 60, LVCFMT_RIGHT},
            {L"Duration", 80, LVCFMT_RIGHT},
        };
        for (int i = 0; i < kColCount; ++i)
        {
            m_list.InsertColumn(i, cols[i].title, cols[i].fmt, cols[i].width);
        }
    }

    // ---- record handling ------------------------------------------------
    LRESULT OnRecord(UINT, WPARAM, LPARAM lp, BOOL&)
    {
        auto* rec = reinterpret_cast<adin::TraceRecord*>(lp);
        if (rec == nullptr)
        {
            return 0;
        }
        if (static_cast<adin::Phase>(rec->phase) == adin::Phase::Request)
        {
            AddRequest(*rec);
        }
        else
        {
            MergeResponse(*rec);
        }
        delete rec;
        UpdateStatus();
        return 0;
    }

    static bool IsBindOp(const wchar_t* op)
    {
        return wcsstr(op, L"bind") != nullptr;
    }
    static bool IsConnectOp(const wchar_t* op)
    {
        return wcsncmp(op, L"ldap_init", 9) == 0 || wcscmp(op, L"ldap_connect") == 0;
    }

    void AddRequest(const adin::TraceRecord& rec)
    {
        const int i = static_cast<int>(m_rows.size());
        m_rows.push_back(rec);
        m_seqIndex[rec.seq] = i;

        wchar_t buf[32] = {};
        swprintf_s(buf, L"%u", rec.seq);
        m_list.InsertItem(i, buf);               // ID
        m_list.SetItemText(i, 1, rec.process);   // Process
        m_list.SetItemText(i, 2, rec.operation); // Request
        if (IsConnectOp(rec.operation))          // Domain Controller
        {
            m_list.SetItemText(i, 3, rec.dn);
        }
        if (IsBindOp(rec.operation)) // User (bind DN)
        {
            m_list.SetItemText(i, 4, rec.dn);
        }
        m_list.SetItemText(i, 5, rec.detail); // Input (request parameters)

        if (m_autoscroll)
        {
            m_list.EnsureVisible(i, FALSE);
        }
    }

    void MergeResponse(const adin::TraceRecord& rec)
    {
        auto it = m_seqIndex.find(rec.seq);
        if (it == m_seqIndex.end())
        {
            return;
        }
        const int i = it->second;
        if (i < 0 || i >= static_cast<int>(m_rows.size()))
        {
            return;
        }
        m_rows[i].resultCode = rec.resultCode;
        m_rows[i].durationUs = rec.durationUs;
        wchar_t buf[32] = {};
        swprintf_s(buf, L"%d", rec.resultCode);
        m_list.SetItemText(i, 7, buf); // Result
        swprintf_s(buf, L"%llu", static_cast<unsigned long long>(rec.durationUs));
        m_list.SetItemText(i, 8, buf); // Duration

        if (m_list.GetSelectedIndex() == i)
        {
            ShowDetail(i);
        }
    }

    static std::wstring FormatTime(uint64_t ft100ns)
    {
        if (ft100ns == 0)
        {
            return std::wstring();
        }
        FILETIME ft = {static_cast<DWORD>(ft100ns & 0xFFFFFFFF), static_cast<DWORD>(ft100ns >> 32)};
        SYSTEMTIME utc = {};
        SYSTEMTIME local = {};
        if (!::FileTimeToSystemTime(&ft, &utc) || !::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        {
            return std::wstring();
        }
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%02d:%02d:%02d.%03d", local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
        return buf;
    }

    LRESULT OnPipeConnect(UINT, WPARAM, LPARAM, BOOL&)
    {
        UpdateStatus();
        return 0;
    }

    LRESULT OnPipeError(UINT, WPARAM, LPARAM lp, BOOL&)
    {
        if (const wchar_t* text = reinterpret_cast<const wchar_t*>(lp))
        {
            ::MessageBoxW(m_hWnd, text, L"AdInsight", MB_ICONWARNING);
        }
        return 0;
    }

    // ---- detail pane ----------------------------------------------------
    LRESULT OnListItemChanged(int, LPNMHDR p, BOOL& handled)
    {
        auto* nm = reinterpret_cast<LPNMLISTVIEW>(p);
        if (p->hwndFrom == m_list.m_hWnd && (nm->uNewState & LVIS_SELECTED) != 0)
        {
            ShowDetail(nm->iItem);
        }
        handled = FALSE;
        return 0;
    }

    void ShowDetail(int i)
    {
        if (i < 0 || i >= static_cast<int>(m_rows.size()))
        {
            return;
        }
        const adin::TraceRecord& r = m_rows[i];
        std::wstring t;
        t += L"Operation:\t";
        t += r.operation;
        t += L"\r\nProcess:\t";
        t += r.process;
        wchar_t num[48] = {};
        swprintf_s(num, L" (PID %u)", r.pid);
        t += num;
        t += L"\r\nDN:\t";
        t += r.dn;
        t += L"\r\nRequest:\t";
        t += r.detail;
        t += L"\r\nResult:\t";
        swprintf_s(num, L"%d", r.resultCode);
        t += num;
        t += L"\r\nDuration:\t";
        swprintf_s(num, L"%llu us", static_cast<unsigned long long>(r.durationUs));
        t += num;
        m_detail.SetWindowTextW(t.c_str());
    }

    // ---- capture --------------------------------------------------------
    LRESULT OnCapture(WORD, WORD, HWND, BOOL&)
    {
        if (m_capturing)
        {
            StopCapture();
        }
        else
        {
            StartCapture();
        }
        SyncMenu();
        UpdateStatus();
        return 0;
    }

    void StartCapture()
    {
        if (m_dllPath.empty())
        {
            m_dllPath = adin::ExtractHookDll();
            if (m_dllPath.empty())
            {
                ::MessageBoxW(m_hWnd, L"Cannot write out extracted DLL", L"AdInsight error", MB_ICONERROR);
                return;
            }
        }
        if (!m_pipe.Start(m_hWnd))
        {
            ::MessageBoxW(m_hWnd, L"Could not open the capture pipe.", L"AdInsight error", MB_ICONERROR);
            return;
        }
        // Load the DLL into our own process and arm it, matching the original's
        // LoadLibrary plus AdmonTraceInit plus AdmonMonitorStart sequence.
        if (m_hook.module == nullptr)
        {
            adin::LoadSelf(m_dllPath, m_hook);
        }
        if (m_hook.traceInit != nullptr)
        {
            m_hook.traceInit(nullptr, nullptr, nullptr, 0);
        }
        if (m_hook.monitorStart != nullptr)
        {
            m_hook.monitorStart();
        }
        m_capturing = true;
    }

    void StopCapture()
    {
        if (m_hook.monitorStop != nullptr)
        {
            m_hook.monitorStop();
        }
        m_pipe.Stop();
        m_capturing = false;
    }

    LRESULT OnAutoscroll(WORD, WORD, HWND, BOOL&)
    {
        m_autoscroll = !m_autoscroll;
        SyncMenu();
        return 0;
    }

    LRESULT OnClear(WORD, WORD, HWND, BOOL&)
    {
        m_list.DeleteAllItems();
        m_rows.clear();
        m_seqIndex.clear();
        m_detail.SetWindowTextW(L"");
        UpdateStatus();
        return 0;
    }

    // Inject the hook DLL into a process by PID. Used only against a benign
    // test process spawned for validation.
    LRESULT OnInject(WORD, WORD, HWND, BOOL&)
    {
        CFindDlg dlg(L"Process id (PID) to inject into:");
        if (dlg.DoModal(m_hWnd) != IDOK || dlg.m_text.empty())
        {
            return 0;
        }
        if (!m_capturing)
        {
            StartCapture();
            SyncMenu();
        }
        const DWORD pid = static_cast<DWORD>(_wtoi(dlg.m_text.c_str()));
        if (pid == 0)
        {
            return 0;
        }
        if (!adin::InjectIntoPid(pid, m_dllPath))
        {
            ::MessageBoxW(m_hWnd, L"Injection into the target process failed.", L"AdInsight", MB_ICONWARNING);
        }
        return 0;
    }

    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        CFindDlg dlg(L"Find what:");
        if (dlg.DoModal(m_hWnd) != IDOK || dlg.m_text.empty())
        {
            return 0;
        }
        std::wstring needle = dlg.m_text;
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        const int start = m_list.GetSelectedIndex();
        const int count = static_cast<int>(m_rows.size());
        for (int step = 1; step <= count; ++step)
        {
            const int i = (start + step) % count;
            if (RowMatches(m_rows[i], needle))
            {
                m_list.SelectItem(i);
                m_list.EnsureVisible(i, FALSE);
                return 0;
            }
        }
        ::MessageBeep(MB_ICONASTERISK);
        return 0;
    }

    static bool RowMatches(const adin::TraceRecord& r, const std::wstring& needle)
    {
        std::wstring hay = std::wstring(r.process) + L" " + r.operation + L" " + r.dn + L" " + r.detail;
        std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
        return hay.find(needle) != std::wstring::npos;
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        const int i = m_list.GetSelectedIndex();
        if (i < 0 || i >= static_cast<int>(m_rows.size()))
        {
            return 0;
        }
        const adin::TraceRecord& r = m_rows[i];
        std::wstring line = std::wstring(r.operation) + L"\t" + r.process + L"\t" + r.dn + L"\t" + r.detail;
        CopyToClipboard(line);
        return 0;
    }

    void CopyToClipboard(const std::wstring& s)
    {
        if (!::OpenClipboard(m_hWnd))
        {
            return;
        }
        ::EmptyClipboard();
        const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void* p = ::GlobalLock(h))
            {
                memcpy(p, s.c_str(), bytes);
                ::GlobalUnlock(h);
                ::SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        ::CloseClipboard();
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        wchar_t path[MAX_PATH] = L"adinsight.txt";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hWnd;
        ofn.lpstrFilter = L"Text files\0*.txt\0All files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (::GetSaveFileNameW(&ofn))
        {
            SaveToFile(path);
        }
        return 0;
    }

    void SaveToFile(const wchar_t* path)
    {
        HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
        {
            return;
        }
        const unsigned char bom[2] = {0xFF, 0xFE};
        DWORD written = 0;
        ::WriteFile(f, bom, 2, &written, nullptr);
        auto emit = [&](const std::wstring& s)
        {
            ::WriteFile(f, s.data(), static_cast<DWORD>(s.size() * sizeof(wchar_t)), &written, nullptr);
        };
        emit(L"#\tProcess\tPID\tOperation\tDN\tTime\tResult\tDuration(us)\r\n");
        for (const adin::TraceRecord& r : m_rows)
        {
            wchar_t head[64] = {};
            swprintf_s(head, L"%u\t", r.seq);
            std::wstring line = head;
            line += std::wstring(r.process) + L"\t";
            swprintf_s(head, L"%u\t", r.pid);
            line += head;
            line += std::wstring(r.operation) + L"\t" + r.dn + L"\t" + FormatTime(r.fileTime) + L"\t";
            swprintf_s(head, L"%d\t%llu\r\n", r.resultCode, static_cast<unsigned long long>(r.durationUs));
            line += head;
            emit(line);
        }
        ::CloseHandle(f);
    }

    // Out of scope, kept for menu fidelity. Documented in analysis/AdInsight.md.
    LRESULT OnSelectColumns(WORD, WORD, HWND, BOOL&)
    {
        ::MessageBoxW(m_hWnd, L"Column selection is out of scope in this recreation.", L"AdInsight",
                      MB_ICONINFORMATION);
        return 0;
    }
    LRESULT OnFilter(WORD, WORD, HWND, BOOL&)
    {
        ::MessageBoxW(m_hWnd, L"Process and class filtering is out of scope in this recreation.", L"AdInsight",
                      MB_ICONINFORMATION);
        return 0;
    }

    LRESULT OnFont(WORD, WORD, HWND, BOOL&)
    {
        LOGFONTW lf = {};
        ::GetObjectW(AtlGetDefaultGuiFont(), sizeof(lf), &lf);
        CHOOSEFONTW cf = {};
        cf.lStructSize = sizeof(cf);
        cf.hwndOwner = m_hWnd;
        cf.lpLogFont = &lf;
        cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
        if (::ChooseFontW(&cf))
        {
            m_listFont = ::CreateFontIndirectW(&lf);
            m_list.SetFont(m_listFont);
            m_detail.SetFont(m_listFont);
        }
        return 0;
    }

    LRESULT OnAlwaysOnTop(WORD, WORD, HWND, BOOL&)
    {
        m_alwaysOnTop = !m_alwaysOnTop;
        SetWindowPos(m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SyncMenu();
        return 0;
    }

    LRESULT OnExit(WORD, WORD, HWND, BOOL&)
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&)
    {
        CAboutDlg().DoModal(m_hWnd);
        return 0;
    }
    LRESULT OnHelp(WORD, WORD, HWND, BOOL&)
    {
        ::ShellExecuteW(m_hWnd, L"open", L"https://learn.microsoft.com/sysinternals/downloads/adinsight", nullptr,
                        nullptr, SW_SHOWNORMAL);
        return 0;
    }

    void SyncMenu()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull())
        {
            return;
        }
        menu.CheckMenuItem(ID_CAPTURE_EVENTS, MF_BYCOMMAND | (m_capturing ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_CAPTURE_AUTOSCROLL, MF_BYCOMMAND | (m_autoscroll ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_OPT_ALWAYSONTOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED));
    }

    void UpdateStatus()
    {
        if (m_hWndStatusBar == nullptr)
        {
            return;
        }
        wchar_t buf[128] = {};
        swprintf_s(buf, L"%s   Events: %d", m_capturing ? L"Capturing" : L"Not capturing",
                   static_cast<int>(m_rows.size()));
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(buf));
    }

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT, CSize)
    {
        if (m_hWndToolBar != nullptr)
        {
            ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
        }
        if (m_hWndStatusBar != nullptr)
        {
            ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);
        }
        CRect rc;
        GetClientRect(&rc);
        const int top = BandHeight(m_hWndToolBar);
        const int bottom = rc.bottom - BandHeight(m_hWndStatusBar);
        const int usable = bottom - top;
        if (usable < 80)
        {
            return;
        }
        if (m_splitY < 60 || m_splitY > usable - 40)
        {
            m_splitY = usable * 7 / 10;
        }
        m_list.SetWindowPos(nullptr, 0, top, rc.right, m_splitY, SWP_NOZORDER);
        const int detailTop = top + m_splitY + kSplitBar;
        m_detail.SetWindowPos(nullptr, 0, detailTop, rc.right, bottom - detailTop, SWP_NOZORDER);
    }

    int BandHeight(HWND h)
    {
        if (h == nullptr || !::IsWindowVisible(h))
        {
            return 0;
        }
        CRect rc;
        ::GetWindowRect(h, &rc);
        return rc.Height();
    }

    CRect SplitterRect()
    {
        CRect rc;
        GetClientRect(&rc);
        const int top = BandHeight(m_hWndToolBar);
        return CRect(0, top + m_splitY, rc.right, top + m_splitY + kSplitBar);
    }

    BOOL OnSetCursor(CWindow wnd, UINT hit, UINT)
    {
        if (wnd.m_hWnd == m_hWnd && hit == HTCLIENT)
        {
            POINT pt = {};
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
        if (SplitterRect().PtInRect(pt))
        {
            m_dragging = true;
            SetCapture();
        }
        else
        {
            SetMsgHandled(FALSE);
        }
    }
    void OnMouseMove(UINT, CPoint pt)
    {
        if (!m_dragging)
        {
            SetMsgHandled(FALSE);
            return;
        }
        m_splitY = pt.y - BandHeight(m_hWndToolBar);
        OnSize(0, CSize());
    }
    void OnLButtonUp(UINT, CPoint)
    {
        if (m_dragging)
        {
            m_dragging = false;
            ReleaseCapture();
        }
        else
        {
            SetMsgHandled(FALSE);
        }
    }

    // ---- config ---------------------------------------------------------
    void LoadConfig()
    {
        HKEY k = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kCfgKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        {
            return;
        }
        DWORD v = 0;
        DWORD cb = sizeof(v);
        DWORD type = 0;
        if (::RegQueryValueExW(k, L"Autoscroll", nullptr, &type, reinterpret_cast<LPBYTE>(&v), &cb) == ERROR_SUCCESS)
        {
            m_autoscroll = v != 0;
        }
        cb = sizeof(v);
        if (::RegQueryValueExW(k, L"AlwaysOnTop", nullptr, &type, reinterpret_cast<LPBYTE>(&v), &cb) == ERROR_SUCCESS)
        {
            m_alwaysOnTop = v != 0;
        }
        WINDOWPLACEMENT wp = {};
        cb = sizeof(wp);
        if (::RegQueryValueExW(k, L"WindowPlacement", nullptr, &type, reinterpret_cast<LPBYTE>(&wp), &cb) ==
                ERROR_SUCCESS &&
            cb == sizeof(wp) && wp.length == sizeof(wp))
        {
            m_placement = wp;
            m_havePlacement = true;
        }
        ::RegCloseKey(k);
    }

    void SaveConfig()
    {
        HKEY k = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kCfgKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) !=
            ERROR_SUCCESS)
        {
            return;
        }
        auto setDw = [&](const wchar_t* name, DWORD val)
        {
            ::RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
        };
        setDw(L"Autoscroll", m_autoscroll ? 1 : 0);
        setDw(L"AlwaysOnTop", m_alwaysOnTop ? 1 : 0);
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(&wp))
        {
            ::RegSetValueExW(k, L"WindowPlacement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof(wp));
        }
        ::RegCloseKey(k);
    }

    void OnDestroy()
    {
        StopCapture();
        SaveConfig();
        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
        {
            pLoop->RemoveMessageFilter(this);
        }
        PostQuitMessage(0);
    }

    CFont m_listFont;
};
