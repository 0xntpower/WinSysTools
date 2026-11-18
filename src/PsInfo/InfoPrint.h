// PsInfo output: the field layout, the product-type naming, and the hotfix,
// software and disk sections, in both the labelled and CSV forms, plus the usage
// body. Mirrors sub_140006670 and sub_1400079C0.
#pragma once

#include "SubReports.h"

namespace psinfo
{
    inline wchar_t g_term = L'\n';

    // A field prints when there is no filter, or its name starts with the filter
    // compared case-insensitively over the filter length. Mirrors the wcsnicmp
    // gate repeated for every field in sub_140006670.
    inline bool FieldWanted(const wchar_t* fieldName)
    {
        if (!g_opt.hasFilter)
        {
            return true;
        }
        return ::_wcsnicmp(fieldName, g_opt.filter.c_str(), g_opt.filter.size()) == 0;
    }

    inline void Label(const wchar_t* caption)
    {
        if (!g_opt.csv)
        {
            std::wprintf(L"%-27s", caption);
        }
    }

    inline const wchar_t* Suffix(long long value)
    {
        return value == 1 ? L"" : L"s";
    }

    inline void PrintProductTypeServerTail(const wchar_t* word, const std::wstring& reg)
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
    inline void PrintProductType(const SystemData& d)
    {
        if (::_wcsicmp(d.productType.c_str(), L"Winnt") == 0)
        {
            std::wprintf(d.verMajor >= 5 ? L"Professional%c" : L"Workstation%c", g_term);
            return;
        }
        if (::_wcsicmp(d.productType.c_str(), L"Servernt") != 0 && ::_wcsicmp(d.productType.c_str(), L"LanmanNt") != 0)
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

    inline void PrintField(const wchar_t* filterName, const wchar_t* label, const wchar_t* format, const wchar_t* value)
    {
        if (!FieldWanted(filterName))
        {
            return;
        }
        Label(label);
        std::wprintf(format, value, g_term);
    }

    inline void PrintInfo(const std::wstring& computer, const SystemData& d)
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
                std::wprintf(L"%lld day%s %lld hour%s %lld minute%s %lld second%s%c", d.days, Suffix(d.days), d.hours,
                             Suffix(d.hours), d.minutes, Suffix(d.minutes), d.seconds, Suffix(d.seconds), g_term);
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
        PrintField(L"Registered organization", L"Registered organization:", L"%s%c", d.registeredOrg.c_str());
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
            const wchar_t* value = d.processorName.empty() ? d.processorId.c_str() : d.processorName.c_str();
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
                std::wprintf(L"%cVolume %-10s %-10s %-20s %10s %10s %6s", g_term, L"Type", L"Format", L"Label", L"Size",
                             L"Free", L"Free");
            }
            for (const auto& v : d.volumeList)
            {
                const double pct = v.total != 0 ? static_cast<double>(static_cast<long long>(v.free)) /
                                                      static_cast<double>(static_cast<long long>(v.total)) * 100.0
                                                : 0.0;
                const std::wstring total = SizeString(v.total);
                const std::wstring free = SizeString(v.free);
                if (g_opt.csv)
                {
                    std::wprintf(L"%c%c:%c%s%c%s%c%s%c%s%c%s%c%.1f%%", g_term, v.letter, g_term, DriveTypeName(v.type),
                                 g_term, v.format.c_str(), g_term, v.label.c_str(), g_term, total.c_str(), g_term,
                                 free.c_str(), g_term, pct);
                }
                else
                {
                    std::wprintf(L"%c    %c: %-10s %-10s %-20s %10s %10s %5.1f%%", g_term, v.letter,
                                 DriveTypeName(v.type), v.format.c_str(), v.label.c_str(), total.c_str(), free.c_str(),
                                 pct);
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

    // The original prints a fixed program name "psinfo" rather than argv[0], and
    // reprints the banner from inside its usage routine. The caller prints the
    // banner, so this reproduces the fixed name and the usage body.
    inline void PrintUsage()
    {
        std::printf("\n%s returns information about a local or remote Windows NT/2000/XP system.\n\n", "PsInfo");
        std::printf("Usage: %s [-h] [-s] [-d] [-c [-t delimiter]] [filter] "
                    "[\\\\computer[,computer[,..]]|@file [-u Username [-p Password]]]\n",
                    "psinfo");
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
} // namespace psinfo
