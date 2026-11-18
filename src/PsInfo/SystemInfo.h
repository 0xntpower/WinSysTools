// PsInfo gathering: the per-computer driver that connects to the target registry,
// collects every field from the registry, memory, and the PDH uptime counter, and
// hands the result to the printer. Mirrors sub_140004D20, sub_140004E70,
// sub_1400035C0.
#pragma once

#include "InfoPrint.h"
#include "SubReports.h"

namespace psinfo
{
    // Strip the registry processor name: drop leading spaces, then truncate just
    // before the token that ends in "Hz". Mirrors the trim in sub_140004E70.
    [[nodiscard]] inline std::wstring TrimProcessorName(std::wstring name)
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

    // Read the "System Up Time" performance counter for the target. Mirrors the
    // PDH path at the end of sub_140004E70. Returns seconds since boot.
    inline bool QueryUptime(const std::wstring& computer, long long& seconds)
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
        if (::PdhAddCounterW(query, path, 0, &counter) == ERROR_SUCCESS &&
            ::PdhCollectQueryData(query) == ERROR_SUCCESS)
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

    inline bool Collect(HKEY root, const std::wstring& computer, SystemData& d)
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
                       L"{4D36E968-E325-11CE-BFC1-08002BE10318}\\00%02d",
                       i);
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

    // Open the registry root for the target: the local hive directly, or the
    // remote machine through RegConnectRegistry with optional impersonation.
    // Mirrors sub_1400035C0 for the remote path.
    inline bool ConnectRoot(const std::wstring& computer, const std::wstring& local, HKEY& root, bool& remote,
                            HANDLE& token)
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
            if (::LogonUserW(user.c_str(), domain.c_str(), g_opt.password.c_str(), LOGON32_LOGON_NEW_CREDENTIALS,
                             LOGON32_PROVIDER_WINNT50, &token))
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

    inline bool PerComputer(const char* computerA, const std::wstring& local)
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
} // namespace psinfo
