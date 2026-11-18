// smsniff.cpp -- entry point, command-line handling and GUI bootstrap for the SmartSniff
// recreation (a source-accurate packet sniffer, x64, requireAdministrator). The GUI mirrors the
// original window (upper conversation list over a lower packet view). The headless modes
// reproduce the /stext|/stab|/stabular|/shtml|/sverhtml|/sxml export, plus /savelangfile,
// /deleteregkey, /nocapdriver, /noloadsettings and /LoadConfig. A /selftest mode runs the
// packet-parser self-check against known vectors.
//
// SECURITY: this build performs no live capture. The working data path is loading packets
// from an .ssp file. See PcapLoader.h and analysis/smsniff.md.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "Packet.h"
#include "Export.h"
#include "PcapLoader.h"
#include "MainFrame.h"

CAppModule _Module;
PcapApi g_pcap;

namespace
{
    std::vector<std::wstring> args()
    {
        std::vector<std::wstring> v;
        int n = 0;
        if (LPWSTR* a = ::CommandLineToArgvW(::GetCommandLineW(), &n))
        {
            for (int i = 1; i < n; ++i) v.emplace_back(a[i]);
            ::LocalFree(a);
        }
        return v;
    }
    int indexOf(const std::vector<std::wstring>& a, const wchar_t* sw)
    {
        for (int i = 0; i < (int)a.size(); ++i) if (_wcsicmp(a[i].c_str(), sw) == 0) return i;
        return -1;
    }
    std::wstring at(const std::vector<std::wstring>& a, int i)
    {
        return (i >= 0 && i < (int)a.size()) ? a[i] : std::wstring();
    }

    bool looksLikeSsp(const std::wstring& s)
    {
        if (s.size() < 4 || s[0] == L'/') return false;
        std::wstring ext = s.substr(s.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
        return ext == L".ssp";
    }

    void sortConns(std::vector<Connection>& rows, const Config& cfg)
    {
        int col = cfg.cols.sortColumn;
        bool asc = cfg.cols.sortAscending;
        DisplayOptions d; d.showOutgoingIncoming = cfg.showOutgoingIncoming;
        std::stable_sort(rows.begin(), rows.end(), [&](const Connection& a, const Connection& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
            int c;
            if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          c = _wcsicmp(a.column(col, d).c_str(), b.column(col, d).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    // /s* <file>: export the current conversation list, then quit. A .ssp argument loads
    // its packets first, so the export carries real data headlessly. The original cannot
    // do this (it exports only live-captured or loaded data), so loading an .ssp here is a
    // deliberate convenience over the original.
    bool runExportCli(const std::vector<std::wstring>& a, bool noLoadSettings)
    {
        ExportFormat fmt{};
        int fmtPos = -1;
        for (size_t i = 0; i < a.size(); ++i)
            if (FormatFromSwitch(a[i], fmt)) { fmtPos = (int)i; break; }
        if (fmtPos < 0) return false;

        std::wstring file = at(a, fmtPos + 1);

        Config cfg;
        if (!noLoadSettings) cfg.load();

        std::vector<Connection> conns;
        for (const std::wstring& s : a)
            if (looksLikeSsp(s))
            {
                std::vector<SsPacket> pkts; uint32_t ip = 0;
                if (LoadSspFile(s, pkts, ip))
                {
                    std::vector<Connection> c = BuildConnections(pkts, ip);
                    conns.insert(conns.end(), c.begin(), c.end());
                }
            }
        sortConns(conns, cfg);

        DisplayOptions d; d.showOutgoingIncoming = cfg.showOutgoingIncoming;
        ExportRequest r;
        r.colDefs = kColumns;
        r.columns = cfg.cols.visible;
        r.reportTitle = L"Packets Summary";
        r.addHeaderLine = cfg.addExportHeaderLine;
        r.rowCount = (int)conns.size();
        r.cell = [&conns, d](int row, int col) { return conns[row].column(col, d); };
        WriteReport(file, r, fmt);
        return true;
    }

    void saveLangFile()
    {
        std::wstring p = Config::DefaultPath();
        size_t dot = p.rfind(L".cfg");
        if (dot != std::wstring::npos) p.replace(dot, std::wstring::npos, L"_lng.ini");
        std::ofstream f(p, std::ios::binary);
        f << "[General]\r\nTranslatorName=\r\nTranslatorURL=\r\n";
    }

    int runGui(int nCmdShow, bool noLoadSettings, bool loadNoCapDriver)
    {
        if (loadNoCapDriver) g_pcap.Load();   // resolve wpcap if present, as the original does

        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noLoadSettings = noLoadSettings;
        frame.m_pcap = &g_pcap;

        // The original's CreateWindowExA passes CW_USEDEFAULT for X and Y and a fixed
        // 640x480 size. Pass that exact geometry so the first launch matches. Saved
        // placement overrides on later launches.
        RECT rc = { (LONG)CW_USEDEFAULT, (LONG)CW_USEDEFAULT, (LONG)CW_USEDEFAULT + 640, (LONG)CW_USEDEFAULT + 480 };
        if (frame.CreateEx(nullptr, rc) == nullptr)
            return 0;
        frame.ShowWindow(frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const auto a = args();
    const bool noLoadSettings = indexOf(a, L"/noloadsettings") >= 0;

    if (int i = indexOf(a, L"/selftest"); i >= 0)
        return RunSelfTest(at(a, i + 1));
    if (indexOf(a, L"/savelangfile") >= 0) { saveLangFile(); return 0; }
    if (indexOf(a, L"/deleteregkey") >= 0)
    {
        ::DeleteFileW(Config::DefaultPath().c_str());
        ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\0xntpower\\SmartSniff");
        return 0;
    }
    if (runExportCli(a, noLoadSettings)) return 0;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"SmartSniff", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    const bool loadNoCapDriver = indexOf(a, L"/nocapdriver") < 0;
    int ret = runGui(nCmdShow, noLoadSettings, loadNoCapDriver);
    _Module.Term();
    return ret;
}
