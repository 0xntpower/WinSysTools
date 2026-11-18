// LoadOrder (console) - shows the order in which Windows loads device drivers
// and services at boot. Source-accurate C++20 console recreation of the
// Sysinternals LoadOrdC64.exe v1.02.
//
// Behaviour recovered with IDA (idalib) from targets/sysinternals/LoadOrdC64.exe:
//   main                sub_140001CB0  (the body IDA names main)
//   enumerate services  sub_1400013B0  (reads HKLM\...\Services, one record each)
//   print one group set sub_140001820  (ServiceGroupOrder\List then GroupOrderList)
//   print the leftovers sub_140001000  (services whose group is not in the List)
//   banner              sub_140001E80
//   eula gate           sub_140002F80 / sub_140003220
//
// The order is computed purely from the registry, the same way the original does
// it. Nothing goes through the Service Control Manager. See analysis/LoadOrder.md
// for the field-to-column mapping and the ordering algorithm in full.
//
// The tool is ANSI end to end, so this recreation reads the registry with the
// ...A functions and prints with the narrow CRT, which reproduces the original's
// bytes exactly. The project is built Unicode, but every call here is explicit.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace
{
    // The row format the original prints, header and data alike. Field widths are
    // 12, 25, 10, 30, 50, 40 with a single separating space. A field wider than its
    // column is not truncated, it pushes the following columns right.
    constexpr const char* kRowFormat = "%-12s %-25s %-10s %-30s %-50s %-40s\n";

    // Sentinel the original stores for an absent Start or Tag value (0xDEADBEEF).
    // It also serves as the catch-all pass marker when ordering a group by tag.
    constexpr DWORD kNoValue = 0xDEADBEEFu;

    // The original keeps a fixed array of 1000 service records (48000 bytes).
    // ponytail: match that cap so a machine with more services truncates the same
    // way the original does rather than silently diverging.
    constexpr size_t kMaxRecords = 1000;

    // Only these three start types are listed, in this order. SERVICE_DEMAND_START
    // and SERVICE_DISABLED are never printed.
    enum class StartType : DWORD
    {
        Boot = 0,
        System = 1,
        Automatic = 2,
    };

    const char* StartName(DWORD start)
    {
        switch (start)
        {
        case 0:  return "Boot";
        case 1:  return "System";
        case 2:  return "Automatic";
        default: return "";
        }
    }

    // RAII wrapper over an HKEY so every opened key is closed on every path.
    class RegKey
    {
    public:
        RegKey() = default;
        ~RegKey()
        {
            if (m_key != nullptr)
            {
                ::RegCloseKey(m_key);
            }
        }
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;

        [[nodiscard]] LSTATUS Open(HKEY root, const char* subKey)
        {
            Close();
            return ::RegOpenKeyExA(root, subKey, 0, KEY_READ, &m_key);
        }
        [[nodiscard]] HKEY Get() const { return m_key; }

        void Close()
        {
            if (m_key != nullptr)
            {
                ::RegCloseKey(m_key);
                m_key = nullptr;
            }
        }

    private:
        HKEY m_key = nullptr;
    };

    struct ServiceRecord
    {
        std::string name;        // the service key name, printed in the Service column
        std::string displayName; // raw DisplayName value, empty when absent
        std::string imagePath;   // raw ImagePath value, empty when absent
        std::string group;       // raw Group value, "n/a" when absent
        DWORD start = kNoValue;
        DWORD tag = kNoValue;
        bool hasDisplay = false;
        bool hasImage = false;
        bool printed = false;
    };

    // Read a REG_SZ / REG_EXPAND_SZ value as a narrow string the way the original
    // does, straight through RegQueryValueExA. Returns false when the value is
    // absent, leaving out untouched.
    [[nodiscard]] bool ReadStringValue(HKEY key, const char* value, std::string& out)
    {
        char buffer[400] = {};
        DWORD cb = sizeof(buffer);
        if (::RegQueryValueExA(key, value, nullptr, nullptr,
                               reinterpret_cast<LPBYTE>(buffer), &cb) != ERROR_SUCCESS)
        {
            return false;
        }
        out.assign(buffer);
        return true;
    }

    // Read a REG_DWORD value, returning the stored dword or kNoValue when absent.
    // Like the original it takes the first four bytes of whatever is stored.
    [[nodiscard]] DWORD ReadDwordValue(HKEY key, const char* value)
    {
        char buffer[400] = {};
        DWORD cb = sizeof(buffer);
        if (::RegQueryValueExA(key, value, nullptr, nullptr,
                               reinterpret_cast<LPBYTE>(buffer), &cb) != ERROR_SUCCESS)
        {
            return kNoValue;
        }
        DWORD result = 0;
        std::memcpy(&result, buffer, sizeof(result));
        return result;
    }

    // Enumerate HKLM\SYSTEM\CurrentControlSet\Services, one record per subkey, in
    // the order RegEnumKeyA returns them. Mirrors sub_1400013B0.
    [[nodiscard]] std::vector<ServiceRecord> EnumerateServices()
    {
        std::vector<ServiceRecord> records;
        RegKey services;
        if (services.Open(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services") != ERROR_SUCCESS)
        {
            return records;
        }

        char name[400] = {};
        for (DWORD i = 0; ::RegEnumKeyA(services.Get(), i, name, 0x190u) == ERROR_SUCCESS; ++i)
        {
            if (records.size() >= kMaxRecords)
            {
                break;
            }
            HKEY raw = nullptr;
            if (::RegOpenKeyExA(services.Get(), name, 0, KEY_READ, &raw) != ERROR_SUCCESS)
            {
                continue;
            }

            ServiceRecord record;
            record.name = name;
            record.start = ReadDwordValue(raw, "Start");

            std::string group;
            if (ReadStringValue(raw, "Group", group))
            {
                record.group = group;
            }
            else
            {
                record.group = "n/a";
            }

            record.tag = ReadDwordValue(raw, "Tag");
            record.hasDisplay = ReadStringValue(raw, "DisplayName", record.displayName);
            record.hasImage = ReadStringValue(raw, "ImagePath", record.imagePath);

            ::RegCloseKey(raw);
            records.push_back(std::move(record));
        }
        return records;
    }

    void PrintRow(DWORD start, const std::string& groupCol, const std::string& tagCol,
                  const ServiceRecord& r)
    {
        const char* display = r.hasDisplay ? r.displayName.c_str() : "";
        const char* image = r.hasImage ? r.imagePath.c_str() : "";
        std::printf(kRowFormat, StartName(start), groupCol.c_str(), tagCol.c_str(),
                    r.name.c_str(), display, image);
    }

    // The Tag column: "n/a*" when the service has no tag, the number alone when the
    // tag was placed by the group's explicit GroupOrderList order, or the number
    // with a trailing "*" when it fell through to the catch-all pass.
    [[nodiscard]] std::string TagColumn(DWORD tag, bool matchedByCatchAll)
    {
        if (tag == kNoValue)
        {
            return "n/a*";
        }
        std::string out = std::to_string(tag);
        if (matchedByCatchAll)
        {
            out += "*";
        }
        return out;
    }

    // Read the tag order for one group from GroupOrderList. The value is a binary
    // blob of [count][tag0][tag1]... The returned passes are the explicit tags in
    // order followed by kNoValue, the catch-all that takes every remaining service
    // in the group. Mirrors the GroupOrderList handling in sub_140001820.
    [[nodiscard]] std::vector<DWORD> GroupTagPasses(const char* group)
    {
        std::vector<DWORD> passes;
        RegKey order;
        DWORD count = 1; // one pass (catch-all only) when the group is not listed
        if (order.Open(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\GroupOrderList") == ERROR_SUCCESS)
        {
            BYTE blob[4000] = {};
            DWORD cb = sizeof(blob);
            if (::RegQueryValueExA(order.Get(), group, nullptr, nullptr, blob, &cb) == ERROR_SUCCESS)
            {
                DWORD dwords = cb >> 2;
                DWORD stated = 0;
                std::memcpy(&stated, blob, sizeof(stated));
                if (cb >= 4 && stated + 1 < dwords)
                {
                    dwords = stated + 1;
                }
                count = dwords;
                for (DWORD k = 1; k < count && k * 4u + 4u <= cb; ++k)
                {
                    DWORD tag = 0;
                    std::memcpy(&tag, blob + k * 4u, sizeof(tag));
                    passes.push_back(tag);
                }
            }
        }
        passes.push_back(kNoValue); // the catch-all pass closes every group
        return passes;
    }

    // Walk a REG_MULTI_SZ into its component strings, stopping at the empty string
    // or at the byte count, matching the original's strchr walk.
    [[nodiscard]] std::vector<std::string> SplitMultiSz(const BYTE* data, DWORD cb)
    {
        std::vector<std::string> out;
        DWORD i = 0;
        while (i < cb && data[i] != 0)
        {
            const char* s = reinterpret_cast<const char*>(data + i);
            std::string item(s);
            i += static_cast<DWORD>(item.size()) + 1;
            out.push_back(std::move(item));
        }
        return out;
    }

    // Print every service of one start type, in load order. First the groups named
    // in ServiceGroupOrder\List, each ordered by its GroupOrderList tags, then the
    // leftovers whose group is not in the List. Mirrors sub_140001820 followed by
    // sub_140001000.
    void PrintStartType(std::vector<ServiceRecord>& records, DWORD start)
    {
        RegKey groupOrder;
        if (groupOrder.Open(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder") == ERROR_SUCCESS)
        {
            BYTE list[10000] = {};
            DWORD cb = sizeof(list);
            if (::RegQueryValueExA(groupOrder.Get(), "List", nullptr, nullptr, list, &cb) == ERROR_SUCCESS)
            {
                for (const std::string& groupName : SplitMultiSz(list, cb))
                {
                    const std::vector<DWORD> passes = GroupTagPasses(groupName.c_str());
                    for (DWORD passTag : passes)
                    {
                        const bool catchAll = passTag == kNoValue;
                        for (ServiceRecord& r : records)
                        {
                            if (r.start == start && !r.printed
                                && ::_stricmp(r.group.c_str(), groupName.c_str()) == 0
                                && (catchAll || r.tag == passTag))
                            {
                                PrintRow(start, r.group, TagColumn(r.tag, catchAll), r);
                                r.printed = true;
                            }
                        }
                    }
                }
            }
        }

        // Leftovers: every remaining service of this start type, each shown with
        // its own group name plus a trailing "*". The catch-all pass takes them all.
        for (ServiceRecord& r : records)
        {
            if (r.start == start && !r.printed)
            {
                PrintRow(start, r.group + "*", TagColumn(r.tag, true), r);
                r.printed = true;
            }
        }
    }

    // ---- banner and eula --------------------------------------------------------

    constexpr const char* kBannerName = "Loadord";
    constexpr const char* kBannerVersion = "1.02";
    constexpr const char* kBannerDescription = "Startup order viewer";
    constexpr const char* kBannerCopyright = "Independent source-accurate recreation of LoadOrder by Sysinternals, through reverse engineering and rewritten in modern C++20.";
    constexpr const char* kBannerCompany = "Author: 0xntpower";

    constexpr const wchar_t* kEulaSubKey = L"Software\\Sysinternals\\LoadOrder";
    constexpr const wchar_t* kEulaValue = L"EulaAccepted";

    [[nodiscard]] bool StdoutIsConsole()
    {
        DWORD mode = 0;
        return ::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
    }

    void PrintBanner()
    {
        // The original writes the banner to stdout when it is a real console and to
        // stderr otherwise, so a captured or redirected stdout stays table-only.
        // A console check, not _isatty, which treats the NUL device as a tty.
        std::FILE* stream = StdoutIsConsole() ? stdout : stderr;
        std::fprintf(stream, "\n%s v%s - %s\n%s\n%s\n\n",
                     kBannerName, kBannerVersion, kBannerDescription,
                     kBannerCopyright, kBannerCompany);
        std::fflush(stream);
    }

    [[nodiscard]] bool EulaAlreadyAccepted()
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kEulaSubKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD size = sizeof(data);
        const LSTATUS status = ::RegQueryValueExW(key, kEulaValue, nullptr, nullptr,
                                                  reinterpret_cast<LPBYTE>(&data), &size);
        ::RegCloseKey(key);
        return status == ERROR_SUCCESS && data != 0;
    }

    void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (::RegCreateKeyW(HKEY_CURRENT_USER, kEulaSubKey, &key) == ERROR_SUCCESS)
        {
            DWORD data = 1;
            ::RegSetValueExW(key, kEulaValue, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&data), sizeof(data));
            ::RegCloseKey(key);
        }
    }

    // Remove a matching switch from argv in place and report whether it was present.
    [[nodiscard]] bool TakeSwitch(int& argc, char** argv, const char* dash, const char* slash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (::_stricmp(argv[i], dash) == 0 || ::_stricmp(argv[i], slash) == 0)
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }
}

int main(int argc, char** argv)
{
    std::setlocale(LC_CTYPE, "");

    const bool noBanner = TakeSwitch(argc, argv, "-nobanner", "/nobanner");
    if (!noBanner)
    {
        PrintBanner();
    }

    // EULA gate. The original pops a GUI dialog on first run, shared with the GUI
    // build. This console recreation honours the same HKCU flag and -accepteula
    // switch like the other Sysinternals CLI tools in this repo.
    const bool accept = TakeSwitch(argc, argv, "-accepteula", "/accepteula");
    if (!EulaAlreadyAccepted() && !accept)
    {
        std::fprintf(stderr, "This tool requires the Sysinternals license to be accepted. Pass -accepteula.\n\n");
        return 1;
    }
    if (accept)
    {
        WriteEulaAccepted();
    }

    std::vector<ServiceRecord> records = EnumerateServices();

    std::printf(kRowFormat, "Start Value", "Group name", "Tag", "Service/Devide",
                "Display Name", "Image path");

    PrintStartType(records, static_cast<DWORD>(StartType::Boot));
    PrintStartType(records, static_cast<DWORD>(StartType::System));
    PrintStartType(records, static_cast<DWORD>(StartType::Automatic));
    return 0;
}
