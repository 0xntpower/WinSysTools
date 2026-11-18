// OfficeIns.cpp -- entry point. Recreation of OfficeIns (x64, v1.20).
// The GUI path mirrors the original WinMain. The command-line "save and quit" mode
// reproduces /stext|/stab|/stabular|/shtml|/sverhtml|/sxml, plus /StartMode,
// /Connected, /RemoteComputer, /savelangfile and /noloadsettings.
#include <atlbase.h>
#include <atlapp.h>
#include <vector>
#include <string>
#include <fstream>
#include "MainFrame.h"
#include "Addin.h"
#include "Export.h"
#include "Columns.h"

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
        for (int i = 0; i < (int)a.size(); ++i)
            if (_wcsicmp(a[i].c_str(), sw) == 0) return i;
        return -1;
    }

    std::wstring at(const std::vector<std::wstring>& a, int i)
    {
        return (i >= 0 && i < (int)a.size()) ? a[i] : std::wstring();
    }

    std::wstring remoteFromArgs(const std::vector<std::wstring>& a)
    {
        int i = indexOf(a, L"/RemoteComputer");
        return (i >= 0) ? at(a, i + 1) : std::wstring();
    }

    std::vector<int> allColumns()
    {
        std::vector<int> c;
        for (int i = 0; i < kColumnCount; ++i) c.push_back(i);
        return c;
    }

    // /StartMode <0-3> <name> and /Connected <0-1> <name>: apply to every add-in whose
    // ProgID / CLSID / dll matches <name>. Mirrors sub_14000B8EC.
    bool runChangeCli(const std::vector<std::wstring>& a, const std::wstring& remote)
    {
        int acted = 0;
        int sm = indexOf(a, L"/StartMode");
        int cn = indexOf(a, L"/Connected");
        if (sm < 0 && cn < 0) return false;

        auto items = EnumerateAddins(remote);
        if (sm >= 0)
        {
            int mode = _wtoi(at(a, sm + 1).c_str());
            std::wstring name = at(a, sm + 2);
            for (auto& it : items) if (AddinMatchesName(it, name)) { SetStartMode(it, mode, remote); acted = 1; }
        }
        if (cn >= 0)
        {
            int yes = _wtoi(at(a, cn + 1).c_str());
            std::wstring name = at(a, cn + 2);
            for (auto& it : items) if (AddinMatchesName(it, name)) { SetConnected(it, yes, remote); acted = 1; }
        }
        return acted != 0;
    }

    // /s* <file>: export the full scan and quit.
    bool runExportCli(const std::vector<std::wstring>& a, const std::wstring& remote)
    {
        for (size_t i = 0; i < a.size(); ++i)
        {
            ExportFormat fmt;
            if (FormatFromSwitch(a[i], fmt))
            {
                std::wstring file = at(a, (int)i + 1);
                if (file.empty()) return true;
                WriteReport(file, EnumerateAddins(remote), allColumns(), fmt);
                return true;
            }
        }
        return false;
    }

    // /savelangfile: write the translation template next to the executable.
    void saveLangFile()
    {
        std::wstring p = CMainFrame::ConfigPath();
        p.replace(p.rfind(L"OfficeIns.cfg"), std::wstring::npos, L"officeins_lng.ini");
        std::ofstream f(p, std::ios::binary);
        f << "[General]\r\nTranslatorName=\r\nTranslatorURL=\r\n\r\n"
             "[Columns]\r\nProgID=ProgID\r\nDescription=Description\r\nSoftware=Software\r\n"
             "StartMode=Start Mode\r\nConnected=Connected\r\nProductName=Product Name\r\n"
             "Version=Version\r\nCompany=Company\r\nFileDescription=File Description\r\n"
             "FileCreatedTime=File Created Time\r\nFilename=Filename\r\nCLSID=CLSID\r\n"
             "NetAddIn=.Net Add-In\r\nRegistryKey=Registry Key\r\nNetVersion=.NET Version\r\n";
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    const auto a = args();
    const std::wstring remote = remoteFromArgs(a);

    if (indexOf(a, L"/savelangfile") >= 0) { saveLangFile(); return 0; }

    bool noSaveLoad = indexOf(a, L"/noloadsettings") >= 0;

    if (runChangeCli(a, remote)) return 0;
    if (runExportCli(a, remote)) return 0;

    PinToLightMode(); // keep controls light, matching the original on a dark-mode system
    ::InitCommonControls();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES };
    ::InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = noSaveLoad;
        frame.m_remote = remote;
        frame.m_startShowCmd = nCmdShow;
        RECT rc = { 0, 0, 640, 480 };  // original CreateWindowExA X,Y,W,H (sub_14000B758)
        if (frame.CreateEx(nullptr, rc) == nullptr)
            return 1;
        frame.ShowWindow(frame.m_startShowCmd);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    return 0;
}
