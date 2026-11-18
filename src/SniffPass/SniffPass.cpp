// SniffPass.cpp -- entry point, command-line handling and GUI bootstrap for the SniffPass
// recreation (a source-accurate password sniffer, x64, requireAdministrator). The GUI mirrors the
// original window: a single owner-data list of captured logins over a toolbar and status
// bar. The original's switches are /noloadsettings, /noreg, /nocapdriver, /savelangfile
// and /deleteregkey. This x64 v1.13 binary carries no /s* save-and-quit switches, so the
// export engine is reached through File > Save in the original. For offline validation
// this recreation adds the standard /s* headless export and a /selftest mode that
// exercises the protocol parsers and the export builder against synthetic data only.
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "Records.h"
#include "CredParser.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

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

    void saveLangFile()
    {
        std::wstring p = Config::DefaultPath();
        size_t dot = p.rfind(L".cfg");
        if (dot != std::wstring::npos) p.replace(dot, std::wstring::npos, L"_lng.ini");
        std::ofstream f(p, std::ios::binary);
        f << "[general]\r\nTranslatorName=\r\nTranslatorURL=\r\n";
    }

    void writeConsole(const std::string& s)
    {
        HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE)
        {
            DWORD w = 0;
            ::WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr);
        }
    }

    // Build a handful of synthetic records so the export builder can be exercised
    // offline. These are the parser's own self test vectors turned into display rows.
    std::vector<PasswordRecord> syntheticRecords()
    {
        struct V { int proto; const char* user; const char* pass; } vs[] = {
            { PROTO_POP3, "alice", "wonderland" },
            { PROTO_FTP,  "bob",   "buildr" },
            { PROTO_IMAP, "carol", "p@ssw0rd" },
            { PROTO_SMTP, "demo",  "s3cr3t" },
            { PROTO_HTTP, "webuser", "w3bp4ss" },
        };
        SYSTEMTIME st{}; st.wYear = 2026; st.wMonth = 11; st.wDay = 17; st.wHour = 12; st.wMinute = 0; st.wSecond = 0;
        std::vector<PasswordRecord> out;
        int idx = 1;
        for (auto& v : vs)
        {
            Credential c;
            c.protocol = v.proto;
            c.localIP = 0x0100007F; c.remoteIP = 0x0101A8C0;
            c.localPort = (uint16_t)(1050 + idx); c.remotePort = 110;
            c.user = v.user; c.pass = v.pass;
            out.push_back(MakeRecord(c, idx++, st));
        }
        return out;
    }

    // /selftest [outdir]: run the protocol parser vectors and write the seven export
    // formats from synthetic rows. Returns process exit code (0 on pass).
    int runSelfTest(const std::vector<std::wstring>& a)
    {
        ::AttachConsole(ATTACH_PARENT_PROCESS);
        std::string report;
        bool ok = RunParserSelfTest(report);
        report += ok ? "parser: ALL PASS\n" : "parser: FAILURES\n";

        std::wstring dir = at(a, indexOf(a, L"/selftest") + 1);
        if (!dir.empty() && dir[0] != L'/')
        {
            if (dir.back() != L'\\' && dir.back() != L'/') dir += L"\\";
            auto recs = syntheticRecords();
            ExportRequest r;
            r.colDefs = kColumns;
            r.columns.clear();
            for (int i = 0; i < kColumnCount; ++i) r.columns.push_back(i);
            r.reportTitle = L"Sniffed Passwords";
            r.addHeaderLine = true;
            r.rowCount = (int)recs.size();
            r.cell = [&recs](int row, int col) { return recs[row].column(col); };

            struct { ExportFormat fmt; const wchar_t* ext; } fmts[] = {
                { ExportFormat::Text, L"stext.txt" }, { ExportFormat::Tab, L"stab.txt" },
                { ExportFormat::Tabular, L"stabular.txt" }, { ExportFormat::HtmlHorizontal, L"shtml.html" },
                { ExportFormat::HtmlVertical, L"sverhtml.html" }, { ExportFormat::Xml, L"sxml.xml" },
                { ExportFormat::Comma, L"scomma.csv" },
            };
            for (auto& f : fmts) WriteReport(dir + L"sp_export_" + f.ext, r, f.fmt);
            report += "export: wrote 7 formats to the given directory\n";

            std::ofstream rf(dir + L"sp_selftest.txt", std::ios::binary);
            rf.write(report.data(), (std::streamsize)report.size());
        }

        writeConsole(report);
        return ok ? 0 : 1;
    }

    // /s* <file>: export the current (empty without capture) list and quit. Standard
    // headless export, added for offline validation.
    bool runExportCli(const std::vector<std::wstring>& a, bool noSaveLoad)
    {
        ExportFormat fmt{};
        int fmtPos = -1;
        for (size_t i = 0; i < a.size(); ++i)
            if (FormatFromSwitch(a[i], fmt)) { fmtPos = (int)i; break; }
        if (fmtPos < 0) return false;

        std::wstring file = at(a, fmtPos + 1);
        Config cfg;
        if (!noSaveLoad) cfg.load();

        ExportRequest r;
        r.colDefs = kColumns;
        r.columns = cfg.cols.visible;
        r.reportTitle = L"Sniffed Passwords";
        r.addHeaderLine = cfg.addExportHeaderLine;
        r.rowCount = 0;
        r.cell = [](int, int) { return std::wstring(); };
        WriteReport(file, r, fmt);
        return true;
    }

    int runGui(int nCmdShow, bool noSaveLoad)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = noSaveLoad;
        // The original CreateWindowExA uses X=0 Y=0 width=640 height=480 with
        // WS_OVERLAPPEDWINDOW, so pass that exact rect. Saved placement overrides later.
        RECT rc = { 0, 0, 640, 480 };
        if (frame.CreateEx(nullptr, rc) == nullptr) return 0;
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

    if (indexOf(a, L"/selftest") >= 0) return runSelfTest(a);
    if (indexOf(a, L"/savelangfile") >= 0) { saveLangFile(); return 0; }
    if (indexOf(a, L"/deleteregkey") >= 0) { ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\0xntpower\\SniffPass"); return 0; }

    const bool noSaveLoad = indexOf(a, L"/noloadsettings") >= 0 || indexOf(a, L"/noreg") >= 0;
    if (runExportCli(a, noSaveLoad)) return 0;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"Error", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);
    int ret = runGui(nCmdShow, noSaveLoad);
    _Module.Term();
    return ret;
}
