// Enumerators.cpp -- read-only enumeration of the in-scope autostart providers.
//
// Each provider is modelled on how the original reads it:
//   Logon          HKLM and HKCU ...\CurrentVersion\Run and RunOnce, the
//                  common and per-user Startup folders.
//   Services       HKLM\System\CurrentControlSet\Services where Type carries a
//                  Win32 service bit and Start is auto or demand.
//   Drivers        the same hive where Type is a kernel or file-system driver.
//   Scheduled Tasks the Task Scheduler 2.0 COM store, every registered task.
//   Winlogon       HKLM\...\Winlogon Userinit, Shell and the related values.
// Nothing here writes, deletes or disables anything.
#include "Enumerators.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <taskschd.h>
#include <atlbase.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "taskschd.lib")

namespace
{
    using namespace ar;

    // ---- small RAII registry key ---------------------------------------
    class RegKey
    {
    public:
        RegKey() = default;
        ~RegKey() { if (m_key) ::RegCloseKey(m_key); }
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;

        bool Open(HKEY root, const wchar_t* sub, REGSAM extra = 0)
        {
            Close();
            return ::RegOpenKeyExW(root, sub, 0, KEY_READ | extra, &m_key) == ERROR_SUCCESS;
        }
        void Close() { if (m_key) { ::RegCloseKey(m_key); m_key = nullptr; } }
        [[nodiscard]] HKEY get() const { return m_key; }
        [[nodiscard]] bool valid() const { return m_key != nullptr; }

        [[nodiscard]] bool QueryString(const wchar_t* name, std::wstring& out) const
        {
            DWORD type = 0;
            DWORD cb = 0;
            if (::RegQueryValueExW(m_key, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS)
            {
                return false;
            }
            if (type != REG_SZ && type != REG_EXPAND_SZ)
            {
                return false;
            }
            std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
            if (::RegQueryValueExW(m_key, name, nullptr, nullptr,
                    reinterpret_cast<LPBYTE>(buf.data()), &cb) != ERROR_SUCCESS)
            {
                return false;
            }
            buf.resize(wcslen(buf.c_str()));
            out = buf;
            return true;
        }

        [[nodiscard]] bool QueryDword(const wchar_t* name, DWORD& out) const
        {
            DWORD type = 0;
            DWORD data = 0;
            DWORD cb = sizeof(data);
            if (::RegQueryValueExW(m_key, name, nullptr, &type,
                    reinterpret_cast<LPBYTE>(&data), &cb) == ERROR_SUCCESS && type == REG_DWORD)
            {
                out = data;
                return true;
            }
            return false;
        }

        [[nodiscard]] std::wstring LastWriteTime() const
        {
            FILETIME ft{};
            if (::RegQueryInfoKeyW(m_key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    nullptr, nullptr, nullptr, nullptr, &ft) != ERROR_SUCCESS)
            {
                return {};
            }
            return FormatFileTime(ft);
        }

        static std::wstring FormatFileTime(const FILETIME& ft)
        {
            if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
            {
                return {};
            }
            SYSTEMTIME utc{};
            SYSTEMTIME local{};
            if (!::FileTimeToSystemTime(&ft, &utc) ||
                !::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
            {
                return {};
            }
            wchar_t d[64]{};
            wchar_t t[64]{};
            ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, d, 64);
            ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &local, nullptr, t, 64);
            return std::wstring(d) + L" " + t;
        }

    private:
        HKEY m_key = nullptr;
    };

    std::wstring Expand(const std::wstring& s)
    {
        if (s.find(L'%') == std::wstring::npos)
        {
            return s;
        }
        const DWORD need = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
        if (!need)
        {
            return s;
        }
        std::wstring out(need, L'\0');
        ::ExpandEnvironmentStringsW(s.c_str(), out.data(), need);
        out.resize(need ? need - 1 : 0);
        return out;
    }

    std::wstring WinDir()
    {
        wchar_t w[MAX_PATH]{};
        ::GetWindowsDirectoryW(w, MAX_PATH);
        return w;
    }

    bool FileExists(const std::wstring& p)
    {
        const DWORD a = ::GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    void Trim(std::wstring& s)
    {
        const size_t b = s.find_first_not_of(L" \t\"");
        const size_t e = s.find_last_not_of(L" \t\"");
        s = (b == std::wstring::npos) ? std::wstring() : s.substr(b, e - b + 1);
    }

    // Turn a configured command line into the full path of its image. Handles a
    // quoted path, an unquoted path that itself contains spaces, a bare file
    // name found on the search path, and the \SystemRoot, system32-relative and
    // \??\ forms used by service and driver ImagePath values.
    std::wstring ResolveImage(const std::wstring& raw)
    {
        std::wstring cmd = raw;
        Trim(cmd);
        if (cmd.empty())
        {
            return {};
        }

        std::wstring path;
        if (raw.find(L'"') == 0)
        {
            const size_t end = raw.find(L'"', 1);
            path = (end == std::wstring::npos) ? raw.substr(1) : raw.substr(1, end - 1);
        }
        else
        {
            path = cmd;
        }

        // Normalise the NT and relative forms before expansion.
        if (path.rfind(L"\\??\\", 0) == 0)
        {
            path = path.substr(4);
        }
        if (_wcsnicmp(path.c_str(), L"\\SystemRoot\\", 12) == 0)
        {
            path = WinDir() + L"\\" + path.substr(12);
        }
        path = Expand(path);

        // The whole string may be an image on its own.
        if (FileExists(path))
        {
            return path;
        }

        // Try the exe for each prefix split at a space, so a path that itself
        // contains spaces is kept whole while the argument tail is dropped. At
        // each boundary the prefix is tested as a full path, a system32-relative
        // path and a bare name on the search path, which covers quoted paths,
        // service and driver images and Run-key commands with arguments.
        auto resolvePrefix = [&](const std::wstring& cand) -> std::wstring
        {
            if (cand.empty())
            {
                return {};
            }
            if (FileExists(cand)) return cand;
            if (FileExists(cand + L".exe")) return cand + L".exe";
            const std::wstring sysRel = WinDir() + L"\\" + cand;
            if (FileExists(sysRel)) return sysRel;
            if (cand.find(L'\\') == std::wstring::npos)
            {
                wchar_t found[MAX_PATH]{};
                if (::SearchPathW(nullptr, cand.c_str(), L".exe", MAX_PATH, found, nullptr))
                {
                    return found;
                }
            }
            return {};
        };

        if (std::wstring whole = resolvePrefix(path); !whole.empty())
        {
            return whole;
        }
        size_t pos = 0;
        while ((pos = path.find(L' ', pos)) != std::wstring::npos)
        {
            if (std::wstring hit = resolvePrefix(path.substr(0, pos)); !hit.empty())
            {
                return hit;
            }
            ++pos;
        }
        return path;
    }

    std::wstring FileModifiedTime(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (path.empty() || !::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        {
            return {};
        }
        return RegKey::FormatFileTime(fad.ftLastWriteTime);
    }

    std::wstring ResolveShortcut(const std::wstring& path);

    // ---- Logon ----------------------------------------------------------
    void ReadRunKey(HKEY root, const wchar_t* sub, REGSAM wow, const wchar_t* label,
                    const wchar_t* jump, std::vector<Location>& out)
    {
        RegKey k;
        if (!k.Open(root, sub, wow))
        {
            return;
        }
        Location loc;
        loc.label = label;
        loc.jumpPath = jump;
        loc.timestamp = k.LastWriteTime();
        loc.category = Category::Logon;

        DWORD index = 0;
        wchar_t name[16384];
        for (;;)
        {
            DWORD cchName = static_cast<DWORD>(std::size(name));
            DWORD type = 0;
            DWORD cbData = 0;
            const LSTATUS st = ::RegEnumValueW(k.get(), index, name, &cchName, nullptr, &type, nullptr, &cbData);
            if (st == ERROR_NO_MORE_ITEMS)
            {
                break;
            }
            ++index;
            if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || cchName == 0)
            {
                continue;
            }
            std::wstring value;
            if (!k.QueryString(name, value) || value.empty())
            {
                continue;
            }
            AutorunEntry e;
            e.name = name;
            e.launchString = value;
            e.imagePath = ResolveImage(value);
            e.timestamp = loc.timestamp;
            e.enabled = true;
            loc.entries.push_back(std::move(e));
        }
        out.push_back(std::move(loc));
    }

    void ReadStartupFolder(int csidl, const wchar_t* label, std::vector<Location>& out)
    {
        wchar_t folder[MAX_PATH]{};
        if (::SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, folder) != S_OK)
        {
            return;
        }
        Location loc;
        loc.label = label;
        loc.jumpPath = folder;
        loc.isRegistry = false;
        loc.category = Category::Logon;
        loc.timestamp = FileModifiedTime(folder);

        std::wstring pattern = std::wstring(folder) + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    continue;
                }
                if (_wcsicmp(fd.cFileName, L"desktop.ini") == 0)
                {
                    continue;
                }
                std::wstring full = std::wstring(folder) + L"\\" + fd.cFileName;
                AutorunEntry e;
                e.name = fd.cFileName;
                e.launchString = full;
                e.imagePath = ResolveShortcut(full);
                e.timestamp = RegKey::FormatFileTime(fd.ftLastWriteTime);
                e.enabled = true;
                loc.entries.push_back(std::move(e));
            } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
        }
        out.push_back(std::move(loc));
    }

    // A .lnk resolves to its target, any other file is its own image.
    std::wstring ResolveShortcut(const std::wstring& path)
    {
        if (path.size() < 4 || _wcsicmp(path.c_str() + path.size() - 4, L".lnk") != 0)
        {
            return path;
        }
        CComPtr<IShellLinkW> link;
        if (FAILED(link.CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER)) || !link)
        {
            return path;
        }
        CComQIPtr<IPersistFile> file(link);
        if (!file || FAILED(file->Load(path.c_str(), STGM_READ)))
        {
            return path;
        }
        wchar_t target[MAX_PATH]{};
        if (SUCCEEDED(link->GetPath(target, MAX_PATH, nullptr, SLGP_UNCPRIORITY)) && target[0])
        {
            return target;
        }
        return path;
    }

    void EnumLogon(std::vector<Location>& out)
    {
        ReadRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
            L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Computer\\HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", out);
        ReadRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0,
            L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
            L"Computer\\HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", out);
        ReadRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
            L"HKLM\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Computer\\HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", out);
        ReadRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
            L"HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Computer\\HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", out);
        ReadRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0,
            L"HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
            L"Computer\\HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", out);
        ReadStartupFolder(CSIDL_COMMON_STARTUP, L"Common Startup Folder", out);
        ReadStartupFolder(CSIDL_STARTUP, L"User Startup Folder", out);
    }

    // ---- Winlogon -------------------------------------------------------
    void EnumWinlogon(std::vector<Location>& out)
    {
        const wchar_t* sub = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
        RegKey k;
        if (!k.Open(HKEY_LOCAL_MACHINE, sub))
        {
            return;
        }
        Location loc;
        loc.label = std::wstring(L"HKLM\\") + sub;
        loc.jumpPath = std::wstring(L"Computer\\HKEY_LOCAL_MACHINE\\") + sub;
        loc.timestamp = k.LastWriteTime();
        loc.category = Category::Winlogon;

        const wchar_t* values[] = { L"Userinit", L"Shell", L"Taskman", L"VmApplet",
                                    L"GinaDLL", L"System", L"AppSetup" };
        for (const wchar_t* v : values)
        {
            std::wstring data;
            if (!k.QueryString(v, data) || data.empty())
            {
                continue;
            }
            // Userinit is a comma-separated list, the first item is the image.
            std::wstring first = data;
            const size_t comma = first.find(L',');
            if (comma != std::wstring::npos)
            {
                first = first.substr(0, comma);
            }
            AutorunEntry e;
            e.name = v;
            e.launchString = data;
            e.imagePath = ResolveImage(first);
            e.timestamp = loc.timestamp;
            e.enabled = true;
            loc.entries.push_back(std::move(e));
        }
        out.push_back(std::move(loc));
    }

    // ---- Services and Drivers -------------------------------------------
    // Service control manager Type flags: the Win32 service bits mark a user-mode
    // service, the lower values mark a kernel or file-system driver.
    constexpr DWORD kServiceWin32 = 0x10 | 0x20;   // own-process | share-process
    constexpr DWORD kStartAuto = 2;
    constexpr DWORD kStartDemand = 3;
    constexpr DWORD kStartDisabled = 4;

    void EnumServices(bool drivers, std::vector<Location>& out)
    {
        const wchar_t* sub = L"SYSTEM\\CurrentControlSet\\Services";
        RegKey root;
        if (!root.Open(HKEY_LOCAL_MACHINE, sub))
        {
            return;
        }
        Location loc;
        loc.label = drivers ? L"HKLM\\System\\CurrentControlSet\\Services (Drivers)"
                            : L"HKLM\\System\\CurrentControlSet\\Services";
        loc.jumpPath = L"Computer\\HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services";
        loc.timestamp = root.LastWriteTime();
        loc.category = drivers ? Category::Drivers : Category::Services;

        DWORD index = 0;
        wchar_t name[256];
        for (;;)
        {
            DWORD cch = static_cast<DWORD>(std::size(name));
            const LSTATUS st = ::RegEnumKeyExW(root.get(), index, name, &cch,
                nullptr, nullptr, nullptr, nullptr);
            if (st == ERROR_NO_MORE_ITEMS)
            {
                break;
            }
            ++index;
            if (st != ERROR_SUCCESS)
            {
                continue;
            }
            RegKey svc;
            if (!svc.Open(root.get(), name))
            {
                continue;
            }
            DWORD type = 0;
            DWORD start = 0;
            if (!svc.QueryDword(L"Type", type) || !svc.QueryDword(L"Start", start))
            {
                continue;
            }
            const bool isWin32 = (type & kServiceWin32) != 0;
            if (drivers)
            {
                if (isWin32)  // keep only the kernel and file-system drivers here
                {
                    continue;
                }
            }
            else
            {
                if (!isWin32)
                {
                    continue;
                }
                if (start != kStartAuto && start != kStartDemand)
                {
                    continue;
                }
            }

            std::wstring image;
            std::wstring serviceDll;
            RegKey params;
            if (svc.QueryString(L"ServiceDll", serviceDll) && !serviceDll.empty())
            {
                image = ResolveImage(serviceDll);
            }
            else if (params.Open(svc.get(), L"Parameters") && params.QueryString(L"ServiceDll", serviceDll) && !serviceDll.empty())
            {
                image = ResolveImage(serviceDll);
            }
            else
            {
                std::wstring imagePath;
                (void)svc.QueryString(L"ImagePath", imagePath);
                image = ResolveImage(imagePath);
            }

            std::wstring display;
            if (!svc.QueryString(L"DisplayName", display) || display.empty() || display[0] == L'@')
            {
                display = name;
            }

            AutorunEntry e;
            e.name = display;
            e.launchString = image;
            e.imagePath = image;
            e.timestamp = svc.LastWriteTime();
            e.enabled = (start != kStartDisabled);
            loc.entries.push_back(std::move(e));
        }

        std::sort(loc.entries.begin(), loc.entries.end(),
            [](const AutorunEntry& a, const AutorunEntry& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
        out.push_back(std::move(loc));
    }

    // ---- Scheduled Tasks ------------------------------------------------
    std::wstring Bstr(BSTR b) { return (b && *b) ? std::wstring(b, ::SysStringLen(b)) : std::wstring(); }

    void WalkTaskFolder(ITaskFolder* folder, Location& loc)
    {
        CComPtr<IRegisteredTaskCollection> tasks;
        if (SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks)) && tasks)
        {
            LONG n = 0;
            tasks->get_Count(&n);
            for (LONG i = 1; i <= n; ++i)
            {
                VARIANT vi;
                ::VariantInit(&vi);
                vi.vt = VT_I4;
                vi.lVal = i;
                CComPtr<IRegisteredTask> rt;
                if (FAILED(tasks->get_Item(vi, &rt)) || !rt)
                {
                    continue;
                }
                AutorunEntry e;

                CComBSTR path;
                if (SUCCEEDED(rt->get_Path(&path)))
                {
                    e.name = Bstr(path);
                }
                TASK_STATE state = TASK_STATE_UNKNOWN;
                rt->get_State(&state);
                e.enabled = (state != TASK_STATE_DISABLED);

                DATE created = 0;
                // The task file's timestamp stands in for the entry time.
                CComPtr<ITaskDefinition> def;
                if (SUCCEEDED(rt->get_Definition(&def)) && def)
                {
                    CComPtr<IActionCollection> acts;
                    if (SUCCEEDED(def->get_Actions(&acts)) && acts)
                    {
                        LONG ac = 0;
                        acts->get_Count(&ac);
                        for (LONG a = 1; a <= ac; ++a)
                        {
                            CComPtr<IAction> action;
                            if (FAILED(acts->get_Item(a, &action)) || !action)
                            {
                                continue;
                            }
                            TASK_ACTION_TYPE at = TASK_ACTION_EXEC;
                            action->get_Type(&at);
                            if (at == TASK_ACTION_EXEC)
                            {
                                if (CComQIPtr<IExecAction> ex = action)
                                {
                                    CComBSTR p;
                                    if (SUCCEEDED(ex->get_Path(&p)))
                                    {
                                        e.launchString = Bstr(p);
                                        e.imagePath = ResolveImage(e.launchString);
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
                (void)created;
                if (!e.name.empty())
                {
                    loc.entries.push_back(std::move(e));
                }
            }
        }

        CComPtr<ITaskFolderCollection> subs;
        if (SUCCEEDED(folder->GetFolders(0, &subs)) && subs)
        {
            LONG n = 0;
            subs->get_Count(&n);
            for (LONG i = 1; i <= n; ++i)
            {
                VARIANT vi;
                ::VariantInit(&vi);
                vi.vt = VT_I4;
                vi.lVal = i;
                CComPtr<ITaskFolder> sub;
                if (SUCCEEDED(subs->get_Item(vi, &sub)) && sub)
                {
                    WalkTaskFolder(sub, loc);
                }
            }
        }
    }

    void EnumScheduledTasks(std::vector<Location>& out)
    {
        Location loc;
        loc.label = L"Task Scheduler";
        loc.jumpPath = WinDir() + L"\\System32\\Tasks";
        loc.isRegistry = false;
        loc.category = Category::ScheduledTasks;

        CComPtr<ITaskService> svc;
        if (SUCCEEDED(svc.CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER)) && svc)
        {
            VARIANT empty;
            ::VariantInit(&empty);
            if (SUCCEEDED(svc->Connect(empty, empty, empty, empty)))
            {
                CComPtr<ITaskFolder> root;
                if (SUCCEEDED(svc->GetFolder(CComBSTR(L"\\"), &root)) && root)
                {
                    WalkTaskFolder(root, loc);
                }
            }
        }
        out.push_back(std::move(loc));
    }
}

namespace ar
{
    std::vector<Location> EnumerateAll()
    {
        std::vector<Location> out;
        EnumLogon(out);
        EnumServices(false, out);
        EnumServices(true, out);
        EnumScheduledTasks(out);
        EnumWinlogon(out);
        return out;
    }
}
