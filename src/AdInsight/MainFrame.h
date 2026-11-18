// MainFrame.h -- the AdInsight main window.
//
// A toolbar and status bar frame a report list of captured LDAP calls over a
// detail pane. Each row is one wldap32 client call reported by the injected
// hook DLL across the \\.\pipe\ADInsightPipe named pipe. The frame is pinned to
// light mode with the shared toolbar fix so the band reads light on a dark
// system, matching the original. Capture start drops and loads the hook DLL and
// opens the pipe server, exactly as the original drops its BINRES hook DLL.
//
// The method bodies live in MainFrame.cpp. This header keeps the class shape:
// the members, the WTL macros, the message map and the method declarations.
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

    BOOL PreTranslateMessage(MSG* pMsg) override;

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

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void BuildColumns();

    // ---- record handling ------------------------------------------------
    LRESULT OnRecord(UINT, WPARAM, LPARAM lp, BOOL&);
    static bool IsBindOp(const wchar_t* op);
    static bool IsConnectOp(const wchar_t* op);
    void AddRequest(const adin::TraceRecord& rec);
    void MergeResponse(const adin::TraceRecord& rec);
    static std::wstring FormatTime(uint64_t ft100ns);
    LRESULT OnPipeConnect(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPipeError(UINT, WPARAM, LPARAM lp, BOOL&);

    // ---- detail pane ----------------------------------------------------
    LRESULT OnListItemChanged(int, LPNMHDR p, BOOL& handled);
    void ShowDetail(int i);

    // ---- capture --------------------------------------------------------
    LRESULT OnCapture(WORD, WORD, HWND, BOOL&);
    void StartCapture();
    void StopCapture();
    LRESULT OnAutoscroll(WORD, WORD, HWND, BOOL&);
    LRESULT OnClear(WORD, WORD, HWND, BOOL&);
    LRESULT OnInject(WORD, WORD, HWND, BOOL&);
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    static bool RowMatches(const adin::TraceRecord& r, const std::wstring& needle);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    void CopyToClipboard(const std::wstring& s);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    void SaveToFile(const wchar_t* path);
    LRESULT OnSelectColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnFilter(WORD, WORD, HWND, BOOL&);
    LRESULT OnFont(WORD, WORD, HWND, BOOL&);
    LRESULT OnAlwaysOnTop(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnHelp(WORD, WORD, HWND, BOOL&);
    void SyncMenu();
    void UpdateStatus();

    // ---- layout ---------------------------------------------------------
    void OnSize(UINT, CSize);
    int BandHeight(HWND h);
    CRect SplitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hit, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);

    // ---- config ---------------------------------------------------------
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();

    CFont m_listFont;
};
