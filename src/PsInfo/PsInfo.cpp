// PsInfo -- prints system information for a local or remote computer. Source
// accurate C++20 console reconstruction of the Sysinternals PsInfo64.exe v1.79.
//
// Behaviour recovered with IDA (idalib) from PsInfo64.exe:
//   main                 sub_140007D10
//   argument parser      sub_140005E20  (-h -s -d -c -t, filter, \\computer, @file, -u, -p)
//   per-computer driver  sub_140004D20  (connect, collect, print)
//   remote connect       sub_1400035C0  (LogonUser + impersonate + RegConnectRegistry)
//   collect              sub_140004E70  (registry fields, hotfix, software, uptime, memory)
//   printer              sub_140006670  (field layout, product-type logic, disk/hotfix/software)
//   updates recursion    sub_140007730  (Software\Microsoft\Updates by Description)
//   ie minor versions    sub_140007520  (Internet Settings MinorVersion to Internet Explorer - x)
//   local disks          sub_1400048F0  (GetLogicalDrives + volume, type, free)
//   cpu-id lookup        sub_140006440  (legacy family or model table, fallback only)
//   usage                sub_1400079C0
//
// The shared PsTools front end (banner, EULA gate, target list, error text) lives
// in ../common/PsTools.h and is reused read-only. PsInfo adds its own tool flags
// (-h -s -d -c -t) and the positional field filter on top of that framework.
//
// The field values come from the registry read through the wide APIs, and the
// output goes through wide printf exactly as the original does, so the wide to
// byte conversion on a redirected stream matches the original byte for byte.

#include "../common/PsTools.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "pdh.lib")

namespace
{
    // Version-resource fields of PsInfo64.exe v1.79, used for the banner and the
    // EULA registry key. The original reads these from its own version resource at
    // run time. Here they are fixed, which does not change the output.
    constexpr pstools::Banner kBanner{
        "PsInfo",
        "1.79",
        "Local and remote system information viewer",
        "Independent source-accurate recreation of PsInfo by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsInfo";

    constexpr REGSAM kRead = KEY_READ;

    // Tool flags, matching the parser globals byte_14004F0E7 and friends.
    struct Options
    {
        bool hotfixes = false;   // -h
        bool software = false;   // -s
        bool disks = false;      // -d
        bool csv = false;        // -c
        wchar_t delimiter = L','; // -t value, default comma (word_14004E190)
        std::wstring filter;     // positional field filter (qword_14004F0E8)
        bool hasFilter = false;
        std::wstring user;       // -u
        std::wstring password;   // -p
        bool haveUser = false;
    };

    Options g_opt;

    // --- Registry helpers --------------------------------------------------

    struct RegKey
    {
        HKEY key = nullptr;

        RegKey() = default;
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;

        ~RegKey()
        {
            if (key != nullptr)
            {
                ::RegCloseKey(key);
            }
        }

        [[nodiscard]] bool Open(HKEY root, const wchar_t* sub, REGSAM access)
        {
            return ::RegOpenKeyExW(root, sub, 0, access, &key) == ERROR_SUCCESS;
        }
    };

    [[nodiscard]] std::wstring ReadString(HKEY key, const wchar_t* name)
    {
        DWORD type = 0;
        DWORD cb = 0;
        if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS || cb == 0)
        {
            return {};
        }
        std::wstring value(cb / sizeof(wchar_t) + 1, L'\0');
        if (::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &cb)
            != ERROR_SUCCESS)
        {
            return {};
        }
        value.resize(::wcsnlen(value.c_str(), value.size()));
        return value;
    }

    [[nodiscard]] DWORD ReadDword(HKEY key, const wchar_t* name)
    {
        DWORD data = 0;
        DWORD cb = sizeof(data);
        DWORD type = 0;
        ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &cb);
        return data;
    }

    // --- Collected data ----------------------------------------------------

    struct Hotfix
    {
        std::wstring name;
        FILETIME written{};
    };

    struct Application
    {
        std::wstring name;
        std::wstring version;
    };

    struct Volume
    {
        wchar_t letter = L'A';
        UINT type = 0;
        std::wstring label;
        std::wstring format;
        unsigned long long total = 0;
        unsigned long long free = 0;
    };

    struct SystemData
    {
        std::wstring productName;
        std::wstring currentType;
        std::wstring currentVersion;
        std::wstring buildNumber;
        std::wstring csdVersion = L"0";
        std::wstring subVersion;
        std::wstring registeredOrg;
        std::wstring registeredOwner;
        std::wstring systemRoot;
        std::wstring videoDriver;
        std::wstring ieVersion;
        std::wstring numProcessors;
        std::wstring processorId;
        std::wstring processorName;
        std::wstring productType;
        std::wstring productSuite;
        DWORD mhz = 0;
        unsigned long long totalPhys = 0;
        int verMajor = 0;
        int verMinor = 0;

        bool uptimeValid = false;
        long long days = 0;
        long long hours = 0;
        long long minutes = 0;
        long long seconds = 0;

        std::vector<Hotfix> hotfixList;
        std::vector<Application> appList;
        std::vector<Volume> volumeList;
    };

    // Strip the registry processor name: drop leading spaces, then truncate just
    // before the token that ends in "Hz". Mirrors the trim in sub_140004E70.
    [[nodiscard]] std::wstring TrimProcessorName(std::wstring name)
    {
        size_t start = 0;
        while (start < name.size() && name[start] == L' ')
        {
            ++start;
        }
        name.erase(0, start);
        const size_t hz = name.find(L"Hz");
        if (hz != std::wstring::npos)
        {
            size_t cut = hz;
            while (cut > 0 && name[cut] != L' ')
            {
                --cut;
            }
            name.resize(cut);
        }
        return name;
    }

    // --- Hotfix sources ----------------------------------------------------

    bool HotfixKnown(const std::vector<Hotfix>& list, const std::wstring& name)
    {
        for (const auto& h : list)
        {
            if (h.name.find(name) != std::wstring::npos)
            {
                return true;
            }
        }
        return false;
    }

    // Recurse Software\Microsoft\Updates: a key with a "Description" value is one
    // hotfix named by that description, a key without it is a container to recurse
    // into. Mirrors sub_140007730.
    void CollectUpdates(HKEY root, const std::wstring& path, std::vector<Hotfix>& out)
    {
        RegKey key;
        if (!key.Open(root, path.c_str(), kRead))
        {
            return;
        }
        wchar_t name[264] = {};
        for (DWORD i = 0; ::RegEnumKeyW(key.key, i, name, 0x208) == ERROR_SUCCESS; ++i)
        {
            RegKey sub;
            if (!sub.Open(key.key, name, kRead))
            {
                continue;
            }
            DWORD cb = 0;
            if (::RegQueryValueExW(sub.key, L"Description", nullptr, nullptr, nullptr, &cb)
                != ERROR_SUCCESS)
            {
                CollectUpdates(root, path + L"\\" + name, out);
                continue;
            }
            Hotfix h;
            h.name = ReadString(sub.key, L"Description");
            ::RegQueryInfoKeyW(sub.key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr, &h.written);
            out.push_back(std::move(h));
        }
    }

    // Enumerate the Hotfix key subkeys, skipping any name already present as a
    // substring of a collected entry. Mirrors the Hotfix branch of sub_140004E70.
    void CollectHotfixKeys(HKEY root, std::vector<Hotfix>& out)
    {
        RegKey key;
        if (!key.Open(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Hotfix", kRead))
        {
            return;
        }
        wchar_t name[264] = {};
        for (DWORD i = 0; ::RegEnumKeyW(key.key, i, name, 0x208) == ERROR_SUCCESS; ++i)
        {
            if (HotfixKnown(out, name))
            {
                continue;
            }
            RegKey sub;
            if (!sub.Open(key.key, name, kRead))
            {
                continue;
            }
            Hotfix h;
            h.name = name;
            ::RegQueryInfoKeyW(sub.key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr, &h.written);
            out.push_back(std::move(h));
        }
    }

    // Split Internet Settings MinorVersion into semicolon separated tokens and add
    // one "Internet Explorer - token" entry per token. Mirrors sub_140007520.
    void CollectIeVersions(HKEY root, std::vector<Hotfix>& out)
    {
        RegKey key;
        if (!key.Open(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", kRead))
        {
            return;
        }
        const std::wstring value = ReadString(key.key, L"MinorVersion");
        size_t start = 0;
        while (start < value.size())
        {
            const size_t semi = value.find(L';', start);
            const std::wstring token = value.substr(
                start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
            if (!token.empty())
            {
                Hotfix h;
                h.name = L"Internet Explorer - " + token;
                out.push_back(std::move(h));
            }
            if (semi == std::wstring::npos)
            {
                break;
            }
            start = semi + 1;
        }
    }

    // --- Software ----------------------------------------------------------

    // Enumerate the Uninstall key and keep the applications sorted ascending by
    // display name with an ordinal compare, inserting before the first element
    // that is not less than the new one, as the original linked list does.
    void CollectSoftware(HKEY root, std::vector<Application>& out)
    {
        RegKey key;
        if (!key.Open(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", kRead))
        {
            return;
        }
        wchar_t name[264] = {};
        for (DWORD i = 0; ::RegEnumKeyW(key.key, i, name, 0x208) == ERROR_SUCCESS; ++i)
        {
            RegKey sub;
            if (::RegOpenKeyW(key.key, name, &sub.key) != ERROR_SUCCESS)
            {
                continue;
            }
            DWORD cb = 0;
            if (::RegQueryValueExW(sub.key, L"DisplayName", nullptr, nullptr, nullptr, &cb)
                != ERROR_SUCCESS)
            {
                continue;
            }
            Application app;
            app.name = ReadString(sub.key, L"DisplayName");
            app.version = ReadString(sub.key, L"DisplayVersion");
            const auto pos = std::lower_bound(
                out.begin(), out.end(), app,
                [](const Application& a, const Application& b)
                {
                    return ::wcscmp(a.name.c_str(), b.name.c_str()) < 0;
                });
            out.insert(pos, std::move(app));
        }
    }

    // --- Disks -------------------------------------------------------------

    const wchar_t* DriveTypeName(UINT type)
    {
        switch (type)
        {
        case DRIVE_NO_ROOT_DIR: return L"No Root";
        case DRIVE_REMOVABLE:   return L"Removable";
        case DRIVE_FIXED:       return L"Fixed";
        case DRIVE_REMOTE:      return L"Remote";
        case DRIVE_CDROM:       return L"CD-ROM";
        case DRIVE_RAMDISK:     return L"RAM Disk";
        default:                return L"Unknown";
        }
    }

    void CollectVolumes(std::vector<Volume>& out)
    {
        const UINT previous = ::SetErrorMode(SEM_FAILCRITICALERRORS);
        const DWORD drives = ::GetLogicalDrives();
        wchar_t root[4] = L"A:\\";
        for (int i = 0; i < 26; ++i)
        {
            if ((drives & (1u << i)) == 0)
            {
                continue;
            }
            root[0] = static_cast<wchar_t>(L'A' + i);
            Volume v;
            v.letter = root[0];
            wchar_t label[MAX_PATH] = {};
            wchar_t fs[32] = {};
            ::GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr, fs, 32);
            v.label = label;
            v.format = fs;
            v.type = ::GetDriveTypeW(root);
            ULARGE_INTEGER freeToCaller = {};
            ULARGE_INTEGER total = {};
            ULARGE_INTEGER totalFree = {};
            ::GetDiskFreeSpaceExW(root, &freeToCaller, &total, &totalFree);
            v.total = total.QuadPart;
            v.free = totalFree.QuadPart;
            out.push_back(std::move(v));
        }
        ::SetErrorMode(previous);
    }

    [[nodiscard]] std::wstring SizeString(unsigned long long bytes)
    {
        wchar_t buf[32] = {};
        if (bytes > 0x40000000ULL)
        {
            ::swprintf(buf, std::size(buf), L"%2.2f GB", static_cast<double>(bytes) / 1073741824.0);
        }
        else if (bytes > 0x100000ULL)
        {
            ::swprintf(buf, std::size(buf), L"%3.2f MB", static_cast<double>(bytes) / 1048576.0);
        }
        else if (bytes != 0)
        {
            ::swprintf(buf, std::size(buf), L"%3.2f KB", static_cast<double>(bytes) / 1024.0);
        }
        return buf;
    }

    // --- Uptime ------------------------------------------------------------

    // Read the "System Up Time" performance counter for the target. Mirrors the
    // PDH path at the end of sub_140004E70. Returns seconds since boot.
    bool QueryUptime(const std::wstring& computer, long long& seconds)
    {
        PDH_HQUERY query = nullptr;
        if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
        {
            return false;
        }
        wchar_t path[MAX_PATH] = {};
        ::swprintf(path, std::size(path), L"\\\\%s\\System\\System Up Time", computer.c_str());
        PDH_HCOUNTER counter = nullptr;
        bool ok = false;
        if (::PdhAddCounterW(query, path, 0, &counter) == ERROR_SUCCESS
            && ::PdhCollectQueryData(query) == ERROR_SUCCESS)
        {
            PDH_FMT_COUNTERVALUE value = {};
            if (::PdhGetFormattedCounterValue(counter, PDH_FMT_LARGE, nullptr, &value) == ERROR_SUCCESS)
            {
                seconds = value.largeValue;
                ok = true;
            }
        }
        ::PdhCloseQuery(query);
        return ok;
    }

    // --- Collect -----------------------------------------------------------

    bool Collect(HKEY root, const std::wstring& computer, SystemData& d)
    {
        RegKey nt;
        if (!nt.Open(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion", kRead))
        {
            return false;
        }
        const std::wstring csd = ReadString(nt.key, L"CSDVersion");
        if (!csd.empty())
        {
            d.csdVersion = csd;
        }
        d.subVersion = ReadString(nt.key, L"SubVersionNumber");
        d.buildNumber = ReadString(nt.key, L"CurrentBuildNumber");
        d.currentType = ReadString(nt.key, L"CurrentType");
        d.currentVersion = ReadString(nt.key, L"CurrentVersion");
        d.productName = ReadString(nt.key, L"ProductName");
        d.registeredOrg = ReadString(nt.key, L"RegisteredOrganization");
        d.registeredOwner = ReadString(nt.key, L"RegisteredOwner");
        d.systemRoot = ReadString(nt.key, L"SystemRoot");

        for (int i = 0; i < 10; ++i)
        {
            wchar_t sub[160] = {};
            ::swprintf(sub, std::size(sub),
                       L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
                       L"{4D36E968-E325-11CE-BFC1-08002BE10318}\\00%02d", i);
            RegKey video;
            if (video.Open(root, sub, kRead))
            {
                d.videoDriver = ReadString(video.key, L"DriverDesc");
                break;
            }
        }

        RegKey ie;
        if (ie.Open(root, L"Software\\Microsoft\\Internet Explorer\\Version Vector", kRead))
        {
            d.ieVersion = ReadString(ie.key, L"IE");
        }

        if (g_opt.hotfixes)
        {
            CollectUpdates(root, L"Software\\Microsoft\\Updates", d.hotfixList);
            CollectHotfixKeys(root, d.hotfixList);
            CollectIeVersions(root, d.hotfixList);
        }
        if (g_opt.software)
        {
            CollectSoftware(root, d.appList);
        }

        RegKey env;
        if (env.Open(root, L"System\\CurrentControlSet\\Control\\Session Manager\\Environment", kRead))
        {
            d.numProcessors = ReadString(env.key, L"number_of_processors");
            d.processorId = ReadString(env.key, L"processor_identifier");
        }

        RegKey po;
        if (po.Open(root, L"System\\CurrentControlSet\\Control\\ProductOptions", kRead))
        {
            d.productType = ReadString(po.key, L"ProductType");
            d.productSuite = ReadString(po.key, L"ProductSuite");
        }

        RegKey cpu;
        if (cpu.Open(root, L"Hardware\\Description\\System\\CentralProcessor\\0", kRead))
        {
            d.mhz = ReadDword(cpu.key, L"~MHz");
            d.processorName = TrimProcessorName(ReadString(cpu.key, L"ProcessorNameString"));
        }

        MEMORYSTATUSEX mem = {};
        mem.dwLength = sizeof(mem);
        ::GlobalMemoryStatusEx(&mem);
        d.totalPhys = mem.ullTotalPhys;

        ::swscanf_s(d.currentVersion.c_str(), L"%d.%d", &d.verMajor, &d.verMinor);

        if (g_opt.disks)
        {
            CollectVolumes(d.volumeList);
        }

        long long upSeconds = 0;
        if (QueryUptime(computer, upSeconds))
        {
            d.uptimeValid = true;
            d.days = upSeconds / 86400;
            d.hours = upSeconds % 86400 / 3600;
            d.minutes = upSeconds % 3600 / 60;
            d.seconds = upSeconds % 60;
        }
        return true;
    }

    // --- Print -------------------------------------------------------------

    wchar_t g_term = L'\n';

    // A field prints when there is no filter, or its name starts with the filter
    // compared case-insensitively over the filter length. Mirrors the wcsnicmp
    // gate repeated for every field in sub_140006670.
    bool FieldWanted(const wchar_t* fieldName)
    {
        if (!g_opt.hasFilter)
        {
            return true;
        }
        return ::_wcsnicmp(fieldName, g_opt.filter.c_str(), g_opt.filter.size()) == 0;
    }

    void Label(const wchar_t* caption)
    {
        if (!g_opt.csv)
        {
            std::wprintf(L"%-27s", caption);
        }
    }

    const wchar_t* Suffix(long long value)
    {
        return value == 1 ? L"" : L"s";
    }

    void PrintProductTypeServerTail(const wchar_t* word, const std::wstring& reg)
    {
        std::wprintf(L"%s", word);
        if (::_wcsicmp(reg.c_str(), L"Lanmannt") == 0)
        {
            std::wprintf(L" (Domain Controller)");
        }
        std::wprintf(L"%c", g_term);
    }

    // The product-type line. The workstation path is the common case. The server
    // path reproduces the suite-driven naming from sub_140006670 and is not
    // exercised on a client machine.
    void PrintProductType(const SystemData& d)
    {
        if (::_wcsicmp(d.productType.c_str(), L"Winnt") == 0)
        {
            std::wprintf(d.verMajor >= 5 ? L"Professional%c" : L"Workstation%c", g_term);
            return;
        }
        if (::_wcsicmp(d.productType.c_str(), L"Servernt") != 0
            && ::_wcsicmp(d.productType.c_str(), L"LanmanNt") != 0)
        {
            std::wprintf(L"%s%c", d.productType.c_str(), g_term);
            return;
        }

        const wchar_t* suite = d.productSuite.c_str();
        const wchar_t* prefix = nullptr;
        bool toTail = false;

        if (::wcsstr(suite, L"Enterprise") != nullptr)
        {
            prefix = L"Enterprise ";
            if (d.verMajor >= 5)
            {
                std::wprintf(d.verMinor >= 2 ? L"Enterprise " : L"Advanced ");
                toTail = true;
            }
        }
        else if (::wcsstr(suite, L"SmallBusiness") != nullptr)
        {
            std::wprintf(L"Small Business ");
            toTail = true;
        }
        else if (::wcsstr(suite, L"Web") != nullptr)
        {
            std::wprintf(L"Web ");
            toTail = true;
        }
        else if (d.verMajor < 5 && ::wcsstr(suite, L"Terminal Server") != nullptr)
        {
            std::wprintf(L"Terminal ");
            toTail = true;
        }
        else if (::wcsstr(suite, L"Datacenter") != nullptr)
        {
            std::wprintf(L"Datacenter ");
            toTail = true;
        }
        else if (d.verMajor < 5)
        {
            PrintProductTypeServerTail(L"Server", d.productType);
            return;
        }
        else if (d.verMinor <= 1)
        {
            toTail = true;
        }
        else
        {
            prefix = L"Standard ";
        }

        if (!toTail && prefix != nullptr)
        {
            std::wprintf(L"%s", prefix);
        }
        if (d.verMajor >= 5 && d.verMinor > 1)
        {
            PrintProductTypeServerTail(L"Edition", d.productType);
            return;
        }
        PrintProductTypeServerTail(L"Server", d.productType);
    }

    void PrintField(const wchar_t* filterName, const wchar_t* label, const wchar_t* format,
                    const wchar_t* value)
    {
        if (!FieldWanted(filterName))
        {
            return;
        }
        Label(label);
        std::wprintf(format, value, g_term);
    }

    void PrintInfo(const std::wstring& computer, const SystemData& d)
    {
        if (g_opt.csv)
        {
            std::wprintf(L"%s%c", computer.c_str(), g_term);
        }
        else
        {
            std::wprintf(L"System information for \\\\%s:%c", computer.c_str(), g_term);
        }

        if (FieldWanted(L"Uptime"))
        {
            Label(L"Uptime:");
            if (d.uptimeValid)
            {
                std::wprintf(L"%lld day%s %lld hour%s %lld minute%s %lld second%s%c",
                             d.days, Suffix(d.days), d.hours, Suffix(d.hours),
                             d.minutes, Suffix(d.minutes), d.seconds, Suffix(d.seconds), g_term);
            }
            else
            {
                std::wprintf(L"Error reading uptime%c", g_term);
            }
        }

        if (FieldWanted(L"Kernel version"))
        {
            Label(L"Kernel version:");
            std::wprintf(L"%s, %s%c", d.productName.c_str(), d.currentType.c_str(), g_term);
        }

        if (FieldWanted(L"Product type"))
        {
            Label(L"Product type:");
            PrintProductType(d);
        }

        PrintField(L"Product version", L"Product version:", L"%s%c", d.currentVersion.c_str());

        if (FieldWanted(L"Service Pack"))
        {
            std::wstring sp = d.csdVersion;
            if (::_wcsnicmp(sp.c_str(), L"Service Pack ", 13) == 0)
            {
                sp.erase(0, 13);
            }
            Label(L"Service pack:");
            std::wprintf(L"%s%s%c", sp.c_str(), d.subVersion.c_str(), g_term);
        }

        PrintField(L"Kernel build number", L"Kernel build number:", L"%s%c", d.buildNumber.c_str());
        PrintField(L"Registered organization", L"Registered organization:", L"%s%c",
                   d.registeredOrg.c_str());
        PrintField(L"Registered owner", L"Registered owner:", L"%s%c", d.registeredOwner.c_str());

        if (FieldWanted(L"IE Version"))
        {
            Label(L"IE version:");
            std::wprintf(L"%s%c", d.ieVersion.empty() ? L"Unknown" : d.ieVersion.c_str(), g_term);
        }

        PrintField(L"System Root", L"System root:", L"%s%c", d.systemRoot.c_str());
        PrintField(L"Processors", L"Processors:", L"%s%c", d.numProcessors.c_str());

        if (FieldWanted(L"Processor speed"))
        {
            const DWORD mhz = 5 * ((d.mhz + 4) / 5);
            Label(L"Processor speed:");
            if (mhz < 1000)
            {
                std::wprintf(L"%d MHz%c", mhz, g_term);
            }
            else
            {
                std::wprintf(L"%d.%d GHz%c", mhz / 1000, mhz % 1000 / 100, g_term);
            }
        }

        if (FieldWanted(L"Processor type"))
        {
            Label(L"Processor type:");
            const wchar_t* value =
                d.processorName.empty() ? d.processorId.c_str() : d.processorName.c_str();
            std::wprintf(L"%s%c", value, g_term);
        }

        if (FieldWanted(L"Physical Memory"))
        {
            Label(L"Physical memory:");
            const unsigned long long mb = ((d.totalPhys >> 20) + 1) & ~1ULL;
            std::wprintf(L"%lld MB%c", static_cast<long long>(mb), g_term);
        }

        if (FieldWanted(L"Video driver"))
        {
            Label(L"Video driver:");
            std::wprintf(L"%s", d.videoDriver.c_str());
        }

        if (g_opt.disks)
        {
            if (!g_opt.csv)
            {
                std::wprintf(L"%cVolume %-10s %-10s %-20s %10s %10s %6s", g_term,
                             L"Type", L"Format", L"Label", L"Size", L"Free", L"Free");
            }
            for (const auto& v : d.volumeList)
            {
                const double pct = v.total != 0
                    ? static_cast<double>(static_cast<long long>(v.free))
                        / static_cast<double>(static_cast<long long>(v.total)) * 100.0
                    : 0.0;
                const std::wstring total = SizeString(v.total);
                const std::wstring free = SizeString(v.free);
                if (g_opt.csv)
                {
                    std::wprintf(L"%c%c:%c%s%c%s%c%s%c%s%c%s%c%.1f%%", g_term, v.letter, g_term,
                                 DriveTypeName(v.type), g_term, v.format.c_str(), g_term,
                                 v.label.c_str(), g_term, total.c_str(), g_term, free.c_str(),
                                 g_term, pct);
                }
                else
                {
                    std::wprintf(L"%c    %c: %-10s %-10s %-20s %10s %10s %5.1f%%", g_term, v.letter,
                                 DriveTypeName(v.type), v.format.c_str(), v.label.c_str(),
                                 total.c_str(), free.c_str(), pct);
                }
            }
        }

        bool firstHotfix = true;
        for (const auto& h : d.hotfixList)
        {
            if (firstHotfix && !g_opt.csv)
            {
                std::wprintf(L"\n%c%-13s %s", g_term, L"Installed", L"HotFix");
            }
            firstHotfix = false;
            wchar_t date[64] = {};
            if (h.written.dwLowDateTime != 0 || h.written.dwHighDateTime != 0)
            {
                SYSTEMTIME st = {};
                ::FileTimeToSystemTime(&h.written, &st);
                ::GetDateFormatW(LOCALE_SYSTEM_DEFAULT, 0, &st, nullptr, date, 64);
            }
            else
            {
                ::wcscpy_s(date, L"n/a");
            }
            std::wprintf(L"%c%-13s %s", g_term, date, h.name.c_str());
        }

        bool firstApp = true;
        for (const auto& a : d.appList)
        {
            if (firstApp && !g_opt.csv)
            {
                std::wprintf(L"%cApplications:", g_term);
            }
            firstApp = false;
            const wchar_t* sep = a.version.empty() ? L"" : L" ";
            std::wprintf(L"%c%s%s%s", g_term, a.name.c_str(), sep, a.version.c_str());
        }

        std::wprintf(L"\n");
    }

    // --- Per-computer driver ----------------------------------------------

    [[nodiscard]] std::wstring Widen(const char* s)
    {
        if (s == nullptr || s[0] == '\0')
        {
            return {};
        }
        const int n = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }

    // Open the registry root for the target: the local hive directly, or the
    // remote machine through RegConnectRegistry with optional impersonation.
    // Mirrors sub_1400035C0 for the remote path.
    bool ConnectRoot(const std::wstring& computer, const std::wstring& local, HKEY& root,
                     bool& remote, HANDLE& token)
    {
        if (::_wcsicmp(computer.c_str(), local.c_str()) == 0)
        {
            root = HKEY_LOCAL_MACHINE;
            remote = false;
            return true;
        }
        remote = true;
        if (g_opt.haveUser)
        {
            std::wstring domain = L".";
            std::wstring user = g_opt.user;
            const size_t slash = user.find(L'\\');
            if (slash != std::wstring::npos)
            {
                domain = user.substr(0, slash);
                user = user.substr(slash + 1);
            }
            if (::LogonUserW(user.c_str(), domain.c_str(), g_opt.password.c_str(),
                             LOGON32_LOGON_NEW_CREDENTIALS, LOGON32_PROVIDER_WINNT50, &token))
            {
                ::ImpersonateLoggedOnUser(token);
            }
        }
        const std::wstring unc = L"\\\\" + computer;
        const LSTATUS st = ::RegConnectRegistryW(unc.c_str(), HKEY_LOCAL_MACHINE, &root);
        if (st != ERROR_SUCCESS)
        {
            std::fwprintf(stderr, L"Could not connect to %s:\n", computer.c_str());
            pstools::PrintSystemError(static_cast<DWORD>(st));
            return false;
        }
        return true;
    }

    bool PerComputer(const char* computerA, const std::wstring& local)
    {
        const std::wstring computer = Widen(computerA);
        if (!g_opt.csv)
        {
            std::fwprintf(stderr, L"Querying information for %s...\r", computer.c_str());
            std::fflush(stderr);
        }

        HKEY root = nullptr;
        bool remote = false;
        HANDLE token = nullptr;
        if (!ConnectRoot(computer, local, root, remote, token))
        {
            if (token != nullptr)
            {
                ::RevertToSelf();
                ::CloseHandle(token);
            }
            return false;
        }

        SystemData data;
        const bool ok = Collect(root, computer, data);

        if (remote && root != nullptr)
        {
            ::RegCloseKey(root);
        }
        if (token != nullptr)
        {
            ::RevertToSelf();
            ::CloseHandle(token);
        }

        if (!ok)
        {
            std::fwprintf(stderr, L"Unable to query system information for %s:\n", computer.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }

        PrintInfo(computer, data);
        return true;
    }

    // The original prints a fixed program name "psinfo" rather than argv[0], and
    // reprints the banner from inside its usage routine. The caller prints the
    // banner, so this reproduces the fixed name and the usage body.
    void PrintUsage()
    {
        std::printf("\n%s returns information about a local or remote Windows NT/2000/XP system.\n\n",
                    "PsInfo");
        std::printf("Usage: %s [-h] [-s] [-d] [-c [-t delimiter]] [filter] "
                    "[\\\\computer[,computer[,..]]|@file [-u Username [-p Password]]]\n", "psinfo");
        std::printf("     -u        Specifies optional user name for login to\n"
                    "               remote computer.\n");
        std::printf("     -p        Specifies password for user name.\n");
        std::printf("     -h        Show installed hotfixes.\n");
        std::printf("     -s        Show installed software.\n");
        std::printf("     -d        Show disk volume information.\n");
        std::printf("     -c        Print in CSV format\n");
        std::printf("     -t        The default delimiter for the -c option is a comma,\n"
                    "               but can be overriden with the specified character. Use\n"
                    "               \"\\t\" to specify tab.\n");
        std::printf("     filter    Psinfo will only show data for the field matching the filter.\n"
                    "               e.g. \"psinfo service\" lists only the service pack field.\n");
        std::printf("     computer  Direct PsInfo to perform the command on the remote\n"
                    "               computer or computers specified. If you omit the computer\n"
                    "               name PsInfo runs the command on the local system, \n"
                    "               and if you specify a wildcard (\\\\*), PsInfo runs the\n"
                    "               command on all computers in the current domain.\n");
        std::printf("     @file     PsInfo will run against the computers listed in the file\n"
                    "               specified.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
    }

    // Remove the PsInfo-specific flags from argv, leaving the shared front-end
    // arguments for pstools::ParseCommandLine. Returns false to print usage.
    bool StripToolFlags(int& argc, char** argv)
    {
        int write = 1;
        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if ((a[0] == '-' || a[0] == '/') && a[1] != '\0' && a[2] == '\0')
            {
                const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[1])));
                if (c == 'h')
                {
                    g_opt.hotfixes = true;
                    continue;
                }
                if (c == 's')
                {
                    g_opt.software = true;
                    continue;
                }
                if (c == 'd')
                {
                    g_opt.disks = true;
                    continue;
                }
                if (c == 'c')
                {
                    g_opt.csv = true;
                    continue;
                }
                if (c == 't')
                {
                    if (!g_opt.csv || i + 1 >= argc)
                    {
                        return false;
                    }
                    const char* value = argv[++i];
                    g_opt.delimiter = (::strcmp(value, "\\t") == 0)
                        ? L'\t'
                        : static_cast<wchar_t>(static_cast<unsigned char>(value[0]));
                    continue;
                }
            }
            argv[write++] = a;
        }
        argc = write;
        return true;
    }
}

int main(int argc, char** argv)
{
    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::printf("Eula declined.\n\n");
        return -1;
    }

    if (!StripToolFlags(argc, argv))
    {
        PrintUsage();
        return -1;
    }

    pstools::Targets t;
    if (!pstools::ParseCommandLine(argc, argv, t))
    {
        PrintUsage();
        return -1;
    }

    if (t.accountSpecified)
    {
        g_opt.filter = Widen(t.account.c_str());
        g_opt.hasFilter = !g_opt.filter.empty();
    }
    if (t.haveUser)
    {
        g_opt.haveUser = true;
        g_opt.user = Widen(t.user.c_str());
        if (!t.havePassword)
        {
            t.password = pstools::PromptHiddenPassword();
        }
        g_opt.password = Widen(t.password.c_str());
    }

    g_term = g_opt.csv ? g_opt.delimiter : L'\n';

    const std::wstring local = Widen(pstools::LocalComputerName().c_str());
    const bool ok = pstools::ForEachComputer(t.computerList, false,
        [&](const char* computer) { return PerComputer(computer, local); });
    return ok ? 0 : -1;
}
