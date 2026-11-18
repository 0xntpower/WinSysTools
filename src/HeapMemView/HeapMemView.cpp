// HeapMemView.cpp -- entry point. Recreation of NirSoft HeapMemView v1.05 (x64).
// WinMain mirrors the original sub_14000C614: it refuses pre-Windows 2000 systems,
// handles /savelangfile and /deleteregkey as quit-early actions, and honours
// /nosaveload. The GUI then runs the main window and its message loop.
#include <atlbase.h>
#include <atlapp.h>
#include <vector>
#include <string>
#include <fstream>
#include "MainFrame.h"
#include "Config.h"

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

    bool hasSwitch(const std::vector<std::wstring>& a, const wchar_t* sw)
    {
        for (const auto& s : a) if (_wcsicmp(s.c_str(), sw) == 0) return true;
        return false;
    }

    // /savelangfile: write the translation template next to the executable.
    void saveLangFile()
    {
        std::wstring p = ConfigPath();
        p.replace(p.rfind(L"HeapMemView.cfg"), std::wstring::npos, L"HeapMemView_lng.ini");
        std::ofstream f(p, std::ios::binary);
        f << "[general]\r\nrtl=0\r\ncharset=\r\nTranslatorName=\r\nTranslatorURL=\r\n";
    }

    // /deleteregkey: remove the legacy registry root the binary still references.
    void deleteRegKey()
    {
        ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\NirSoft\\HeapMemView");
    }

    bool preVistaOrOlderUnsupported()
    {
        OSVERSIONINFOW vi{ sizeof(vi) };
#pragma warning(push)
#pragma warning(disable : 4996)
        ::GetVersionExW(&vi);
#pragma warning(pop)
        // The original requires Windows 2000 or later (platform NT, major >= 5).
        return !(vi.dwPlatformId == VER_PLATFORM_WIN32_NT && vi.dwMajorVersion >= 5);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    const auto a = args();

    if (preVistaOrOlderUnsupported())
    {
        ::MessageBoxW(nullptr, L"This utility cannot work on operating system earlier than Windows 2000",
                      L"Error", MB_ICONERROR);
        return 0;
    }

    if (hasSwitch(a, L"/savelangfile")) { saveLangFile(); return 0; }
    if (hasSwitch(a, L"/deleteregkey")) { deleteRegKey(); return 0; }
    const bool noSaveLoad = hasSwitch(a, L"/nosaveload");

    PinToLightMode();
    ::InitCommonControls();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES };
    ::InitCommonControlsEx(&icc);
    ::CoInitialize(nullptr);

    _Module.Init(nullptr, hInstance);
    {
        CMessageLoop loop;
        _Module.AddMessageLoop(&loop);

        CMainFrame frame;
        frame.m_noSaveLoad = noSaveLoad;
        RECT rc = { 0, 0, 640, 480 }; // original CreateWindowExA launches at (0,0), 640x480
        if (frame.CreateEx(nullptr, rc) == nullptr)
            return 1;
        frame.ShowWindow(nCmdShow);
        frame.UpdateWindow();

        loop.Run();
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    ::CoUninitialize();
    return 0;
}
