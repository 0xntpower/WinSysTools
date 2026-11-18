// Dialogs.h -- the About box, the per-process property sheet and the
// Find Handle or DLL dialog.
//
// The property sheet carries the Image, Threads and TCP/IP tabs. Image shows
// the path, command line, current directory, user, start time and parent.
// Threads lists each thread's id, CPU and start address. TCP/IP lists the
// process's connections through GetExtendedTcpTable. The remaining original
// tabs (Performance, GPU, Disk and Network, .NET, Environment, Strings,
// Security, Job) are out of scope.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atldlgs.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include "resource.h"
#include "ProcessData.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace pe
{
    inline std::wstring FormatFileTime(uint64_t ft100ns)
    {
        if (ft100ns == 0)
        {
            return std::wstring();
        }
        FILETIME ft{ static_cast<DWORD>(ft100ns & 0xFFFFFFFF), static_cast<DWORD>(ft100ns >> 32) };
        SYSTEMTIME utc{}, local{};
        if (!::FileTimeToSystemTime(&ft, &utc) || !::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        {
            return std::wstring();
        }
        wchar_t buf[64] = {};
        swprintf(buf, 64, L"%02d:%02d:%02d %d/%d/%04d",
                 local.wHour, local.wMinute, local.wSecond, local.wMonth, local.wDay, local.wYear);
        return buf;
    }

    inline std::wstring GroupNumber(uint64_t v)
    {
        wchar_t raw[32] = {};
        swprintf(raw, 32, L"%llu", static_cast<unsigned long long>(v));
        std::wstring digits = raw, out;
        int c = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        {
            if (c && c % 3 == 0) out.push_back(L',');
            out.push_back(*it);
            ++c;
        }
        std::reverse(out.begin(), out.end());
        return out;
    }
}

// ---- About ---------------------------------------------------------------
class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
    enum { IDD = IDD_ABOUT };

    BEGIN_MSG_MAP(CAboutDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnClose)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
        NOTIFY_CODE_HANDLER(NM_CLICK, OnLink)
        NOTIFY_CODE_HANDLER(NM_RETURN, OnLink)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About Process Explorer");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"Process Explorer v16.0");
        // A lone & is a mnemonic prefix in a static control, so the ampersand
        // is doubled to render as a single &.
        SetDlgItemTextW(IDC_ABOUT_DESC,
            L"Independent source-accurate recreation of Process Explorer by Sysinternals, "
            L"through reverse engineering and rewritten in modern C++20 && WTL 10.\r\n"
            L"Author: 0xntpower");
        return TRUE;
    }

    LRESULT OnLink(int, LPNMHDR p, BOOL&)
    {
        if (p->idFrom == IDC_ABOUT_LINK)
        {
            ::ShellExecuteW(m_hWnd, L"open", L"https://www.sysinternals.com", nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ---- Image page ----------------------------------------------------------
class CImagePage : public CPropertyPageImpl<CImagePage>
{
public:
    enum { IDD = IDD_PROP_IMAGE };
    const pe::ProcRow& m_row;
    std::wstring m_parentName;
    HICON m_icon = nullptr;

    explicit CImagePage(const pe::ProcRow& row, std::wstring parentName)
        : m_row(row), m_parentName(std::move(parentName)) {}

    BEGIN_MSG_MAP(CImagePage)
        MSG_WM_INITDIALOG(OnInitDialog)
        CHAIN_MSG_MAP(CPropertyPageImpl<CImagePage>)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetDlgItemTextW(IDC_IMG_DESC, m_row.description.empty() ? m_row.name.c_str() : m_row.description.c_str());
        SetDlgItemTextW(IDC_IMG_COMPANY, m_row.company.c_str());
        SetDlgItemTextW(IDC_IMG_VERSION, m_row.version.empty() ? L"" : (L"Version: " + m_row.version).c_str());
        SetDlgItemTextW(IDC_IMG_PATH, m_row.imagePath.c_str());

        std::wstring cmd, cwd, user = m_row.user;
        if (HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, m_row.pid))
        {
            cmd = pe::ProcessCommandLine(h);
            cwd = pe::ProcessCurrentDir(h);
            ::CloseHandle(h);
        }
        if (cmd.empty())
        {
            if (HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_row.pid))
            {
                cmd = pe::ProcessCommandLine(h);
                ::CloseHandle(h);
            }
        }
        SetDlgItemTextW(IDC_IMG_CMDLINE, cmd.c_str());
        SetDlgItemTextW(IDC_IMG_CURDIR, cwd.c_str());

        std::wstring parent = m_parentName;
        wchar_t pbuf[64] = {};
        swprintf(pbuf, 64, L" (%u)", m_row.parentPid);
        if (!parent.empty()) parent += pbuf;
        SetDlgItemTextW(IDC_IMG_PARENT, parent.c_str());
        SetDlgItemTextW(IDC_IMG_USER, user.c_str());
        SetDlgItemTextW(IDC_IMG_STARTED, pe::FormatFileTime(m_row.createTime).c_str());
        wchar_t pidbuf[32] = {};
        swprintf(pidbuf, 32, L"%u", m_row.pid);
        SetDlgItemTextW(IDC_IMG_PID, pidbuf);

        if (!m_row.imagePath.empty())
        {
            ::ExtractIconExW(m_row.imagePath.c_str(), 0, nullptr, &m_icon, 1);
            if (m_icon)
            {
                ::SendMessageW(GetDlgItem(IDC_IMG_ICON), STM_SETICON, reinterpret_cast<WPARAM>(m_icon), 0);
            }
        }
        return TRUE;
    }

    ~CImagePage()
    {
        if (m_icon) ::DestroyIcon(m_icon);
    }
};

// ---- Threads page --------------------------------------------------------
class CThreadsPage : public CPropertyPageImpl<CThreadsPage>
{
public:
    enum { IDD = IDD_PROP_THREADS };
    const pe::ProcRow& m_row;
    CListViewCtrl m_list;

    explicit CThreadsPage(const pe::ProcRow& row) : m_row(row) {}

    BEGIN_MSG_MAP(CThreadsPage)
        MSG_WM_INITDIALOG(OnInitDialog)
        CHAIN_MSG_MAP(CPropertyPageImpl<CThreadsPage>)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_THR_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"TID", LVCFMT_RIGHT, 60);
        m_list.InsertColumn(1, L"CPU", LVCFMT_RIGHT, 55);
        m_list.InsertColumn(2, L"Start Address", LVCFMT_LEFT, 130);
        int r = 0;
        for (const pe::ThreadRow& t : m_row.threads)
        {
            wchar_t b[32] = {};
            swprintf(b, 32, L"%u", t.tid);
            m_list.InsertItem(r, b);
            swprintf(b, 32, t.cpu >= 0.005 ? L"%.2f" : L"", t.cpu);
            m_list.SetItemText(r, 1, b);
            swprintf(b, 32, L"0x%016llX", static_cast<unsigned long long>(t.startAddr));
            m_list.SetItemText(r, 2, b);
            ++r;
        }
        return TRUE;
    }
};

// ---- TCP/IP page ---------------------------------------------------------
class CTcpipPage : public CPropertyPageImpl<CTcpipPage>
{
public:
    enum { IDD = IDD_PROP_TCPIP };
    DWORD m_pid;
    CListViewCtrl m_list;

    explicit CTcpipPage(DWORD pid) : m_pid(pid) {}

    BEGIN_MSG_MAP(CTcpipPage)
        MSG_WM_INITDIALOG(OnInitDialog)
        CHAIN_MSG_MAP(CPropertyPageImpl<CTcpipPage>)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_TCP_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Protocol", LVCFMT_LEFT, 55);
        m_list.InsertColumn(1, L"Local Address", LVCFMT_LEFT, 130);
        m_list.InsertColumn(2, L"Remote Address", LVCFMT_LEFT, 130);
        m_list.InsertColumn(3, L"State", LVCFMT_LEFT, 90);
        Populate();
        return TRUE;
    }

    static const wchar_t* StateText(DWORD s)
    {
        switch (s)
        {
        case MIB_TCP_STATE_CLOSED: return L"CLOSED";
        case MIB_TCP_STATE_LISTEN: return L"LISTEN";
        case MIB_TCP_STATE_SYN_SENT: return L"SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return L"SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return L"ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return L"FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2: return L"FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return L"CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return L"CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return L"LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return L"DELETE_TCB";
        default: return L"";
        }
    }

    void Populate()
    {
        int row = 0;
        AddTcp4(row);
        AddTcp6(row);
    }

    void AddTcp4(int& row)
    {
        ULONG size = 0;
        if (::GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER)
        {
            return;
        }
        std::vector<BYTE> buf(size);
        if (::GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
        {
            return;
        }
        auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const MIB_TCPROW_OWNER_PID& e = table->table[i];
            if (e.dwOwningPid != m_pid)
            {
                continue;
            }
            wchar_t local[64] = {}, remote[64] = {};
            IN_ADDR la{}; la.S_un.S_addr = e.dwLocalAddr;
            IN_ADDR ra{}; ra.S_un.S_addr = e.dwRemoteAddr;
            wchar_t ip[48] = {};
            ::InetNtopW(AF_INET, &la, ip, 48);
            swprintf(local, 64, L"%s:%u", ip, ntohs(static_cast<USHORT>(e.dwLocalPort)));
            ::InetNtopW(AF_INET, &ra, ip, 48);
            swprintf(remote, 64, L"%s:%u", ip, ntohs(static_cast<USHORT>(e.dwRemotePort)));
            m_list.InsertItem(row, L"TCP");
            m_list.SetItemText(row, 1, local);
            m_list.SetItemText(row, 2, (e.dwState == MIB_TCP_STATE_LISTEN) ? L"" : remote);
            m_list.SetItemText(row, 3, StateText(e.dwState));
            ++row;
        }
    }

    void AddTcp6(int& row)
    {
        ULONG size = 0;
        if (::GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER)
        {
            return;
        }
        std::vector<BYTE> buf(size);
        if (::GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
        {
            return;
        }
        auto* table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const MIB_TCP6ROW_OWNER_PID& e = table->table[i];
            if (e.dwOwningPid != m_pid)
            {
                continue;
            }
            wchar_t local[80] = {}, remote[80] = {}, ip[64] = {};
            ::InetNtopW(AF_INET6, const_cast<UCHAR*>(e.ucLocalAddr), ip, 64);
            swprintf(local, 80, L"[%s]:%u", ip, ntohs(static_cast<USHORT>(e.dwLocalPort)));
            ::InetNtopW(AF_INET6, const_cast<UCHAR*>(e.ucRemoteAddr), ip, 64);
            swprintf(remote, 80, L"[%s]:%u", ip, ntohs(static_cast<USHORT>(e.dwRemotePort)));
            m_list.InsertItem(row, L"TCPV6");
            m_list.SetItemText(row, 1, local);
            m_list.SetItemText(row, 2, (e.dwState == MIB_TCP_STATE_LISTEN) ? L"" : remote);
            m_list.SetItemText(row, 3, StateText(e.dwState));
            ++row;
        }
    }
};

// ---- Property sheet ------------------------------------------------------
class CProcPropSheet : public CPropertySheetImpl<CProcPropSheet>
{
public:
    CImagePage   m_image;
    CThreadsPage m_threads;
    CTcpipPage   m_tcpip;

    CProcPropSheet(const pe::ProcRow& row, const std::wstring& parentName)
        : m_image(row, parentName), m_threads(row), m_tcpip(row.pid)
    {
        std::wstring title = (row.name.empty() ? L"Process" : row.name) + L" Properties";
        SetTitle(title.c_str());
        AddPage(m_image);
        AddPage(m_threads);
        AddPage(m_tcpip);
        m_psh.dwFlags |= PSH_NOAPPLYNOW;
    }
};

// ---- Find Handle or DLL --------------------------------------------------
// Search every process's handles and modules for a substring, matching the
// original's Find Handle or DLL feature.
class CFindHandleDlg : public CDialogImpl<CFindHandleDlg>
{
public:
    enum { IDD = IDD_FINDHANDLE };
    const std::vector<pe::ProcRow>& m_procs;
    CListViewCtrl m_list;

    explicit CFindHandleDlg(const std::vector<pe::ProcRow>& procs) : m_procs(procs) {}

    BEGIN_MSG_MAP(CFindHandleDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDC_FIND_SEARCH, OnSearch)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_FIND_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Process", LVCFMT_LEFT, 130);
        m_list.InsertColumn(1, L"PID", LVCFMT_RIGHT, 55);
        m_list.InsertColumn(2, L"Type", LVCFMT_LEFT, 70);
        m_list.InsertColumn(3, L"Name", LVCFMT_LEFT, 280);
        return TRUE;
    }

    LRESULT OnSearch(WORD, WORD, HWND, BOOL&)
    {
        wchar_t needle[260] = {};
        GetDlgItemTextW(IDC_FIND_EDIT, needle, 260);
        std::wstring want = needle;
        if (want.empty())
        {
            return 0;
        }
        std::transform(want.begin(), want.end(), want.begin(), towlower);
        m_list.DeleteAllItems();
        ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        int row = 0;
        for (const pe::ProcRow& p : m_procs)
        {
            for (const pe::ModuleRow& mod : pe::EnumModules(p.pid))
            {
                std::wstring hay = mod.path;
                std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
                if (hay.find(want) != std::wstring::npos)
                {
                    AddHit(row, p, L"DLL", mod.path);
                }
            }
            for (const pe::HandleRow& h : pe::EnumHandles(p.pid))
            {
                if (h.name.empty())
                {
                    continue;
                }
                std::wstring hay = h.name;
                std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
                if (hay.find(want) != std::wstring::npos)
                {
                    AddHit(row, p, h.type.c_str(), h.name);
                }
            }
        }
        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        return 0;
    }

    void AddHit(int& row, const pe::ProcRow& p, const wchar_t* type, const std::wstring& name)
    {
        m_list.InsertItem(row, p.name.c_str());
        wchar_t b[32] = {};
        swprintf(b, 32, L"%u", p.pid);
        m_list.SetItemText(row, 1, b);
        m_list.SetItemText(row, 2, type);
        m_list.SetItemText(row, 3, name.c_str());
        ++row;
    }

    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};
