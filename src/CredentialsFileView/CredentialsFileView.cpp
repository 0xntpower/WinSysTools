// CredentialsFileView.cpp -- entry point, command-line handling and GUI
// bootstrap for the CredentialsFileView recreation (NirSoft Credentials file
// decryptor, x64).
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "resource.h"
#include "Config.h"
#include "Columns.h"
#include "CredRecord.h"
#include "CredFile.h"
#include "Export.h"
#include "MainFrame.h"

CAppModule _Module;

namespace
{
    bool ieq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

    std::wstring expand(const std::wstring& s)
    {
        DWORD n = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
        if (!n) return s;
        std::wstring out(n, L'\0');
        ::ExpandEnvironmentStringsW(s.c_str(), out.data(), n);
        out.resize(n - 1);
        return out;
    }

    // Fill the default current-user Credentials folders when the config has none,
    // the same set the Decryption Options dialog auto-fills.
    void fillDefaultFolders(Config& cfg)
    {
        if (cfg.credFolder1.empty()) cfg.credFolder1 = expand(L"%LOCALAPPDATA%\\Microsoft\\Credentials");
        if (cfg.credFolder2.empty()) cfg.credFolder2 = expand(L"%APPDATA%\\Microsoft\\Credentials");
    }

    void writeConsole(const std::wstring& line)
    {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::GetLastError() == ERROR_INVALID_HANDLE)
            ::AllocConsole();
        HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE)
        {
            std::wstring s = line + L"\r\n";
            DWORD written = 0;
            ::WriteConsoleW(h, s.c_str(), static_cast<DWORD>(s.size()), &written, nullptr);
        }
    }

    // Returns true when a headless command (self-check or export) was handled.
    bool runCommandLine(int& exitCode)
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (!argv) return false;

        bool selfTest = false;
        bool noSaveLoad = false;
        bool deleteKey = false;
        bool saveLang = false;
        std::wstring cfgFile;
        ExportFormat fmt{};
        std::wstring outFile;
        bool haveExport = false;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring a = argv[i];
            auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : std::wstring(); };

            if (ieq(a, L"/selftest"))        selfTest = true;
            else if (ieq(a, L"/nosaveload")) noSaveLoad = true;
            else if (ieq(a, L"/deleteregkey")) deleteKey = true;
            else if (ieq(a, L"/savelangfile")) saveLang = true;
            else if (ieq(a, L"/cfg"))        cfgFile = next();
            else if (ieq(a, L"/StartDecrypt")) { /* auto-start, handled in GUI */ }
            else
            {
                ExportFormat f{};
                if (FormatFromSwitch(a, f)) { fmt = f; outFile = next(); haveExport = true; }
            }
        }
        ::LocalFree(argv);

        if (selfTest)
        {
            std::wstring detail;
            bool ok = CredSelfTest(detail);
            writeConsole(L"CredentialsFileView decoder " + detail);
            exitCode = ok ? 0 : 1;
            return true;
        }

        if (deleteKey)
        {
            Config().deleteConfig();
            exitCode = 0;
            return true;
        }

        if (saveLang)
        {
            // Language-file generation is not reconstructed. Treat as a no-op exit.
            exitCode = 0;
            return true;
        }

        if (!haveExport) return false;

        Config cfg;
        if (!noSaveLoad) cfg.load();
        if (!cfgFile.empty()) cfg.loadIni(cfgFile);
        fillDefaultFolders(cfg);

        DecodeRequest req;
        req.source = cfg.dataSource;
        req.credFolders = cfg.folders();
        req.display.showTimeInGmt = cfg.showTimeInGmt;

        std::vector<CredRecord> items;
        DecodeFolders(req, items);
        bool ok = WriteReport(outFile, items, cfg.visibleColumns, fmt, ExportOptions{ cfg.saveFileEncoding });
        exitCode = ok ? 0 : 1;
        return true;
    }

    bool hasSwitch(const wchar_t* sw)
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        bool found = false;
        for (int i = 1; i < argc && argv; ++i)
            if (_wcsicmp(argv[i], sw) == 0) found = true;
        if (argv) ::LocalFree(argv);
        return found;
    }

    int runGui(int nCmdShow)
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        // The original's CreateWindowExW passes CW_USEDEFAULT for X, Y, width and
        // height, so let WTL use its default rect.
        if (frame.CreateEx(nullptr) == nullptr)
            return 0;
        frame.ShowWindow(frame.m_cfg.havePlacement ? frame.m_cfg.placement.showCmd : nCmdShow);
        frame.UpdateWindow();

        // The original shows the Credentials Decryption Options dialog at startup.
        if (hasSwitch(L"/StartDecrypt"))
        {
            fillDefaultFolders(frame.m_cfg);
            frame.Refresh();
        }
        else
        {
            frame.PromptAndDecrypt();
        }

        int ret = loop.Run();
        _Module.RemoveMessageLoop();
        return ret;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    PinToLightMode();
    if (!::InitCommonControlsEx(&icc))
    {
        ::MessageBoxW(nullptr, L"Error: Cannot load the common control classes.", L"CredentialsFileView", MB_ICONERROR);
        return 1;
    }

    _Module.Init(nullptr, hInstance);

    int exitCode = 0;
    if (runCommandLine(exitCode))
    {
        _Module.Term();
        return exitCode;
    }

    int ret = runGui(nCmdShow);
    _Module.Term();
    return ret;
}
