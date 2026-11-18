// PsLogList -- dumps the records of an event log on a local or remote NT system
// through the classic ReadEventLog API, resolving each record's message from the
// source's registered message DLLs. Source-accurate C++20 console reconstruction
// of the Sysinternals psloglist64.exe v2.82.
//
// Behaviour recovered with IDA (idalib) from psloglist64.exe:
//   main                 0x140009EC0  OS gate, banner+EULA, parse, run per computer
//   banner + EULA driver 0x140003E90  (version-resource banner, EULA gate)
//   command-line parse   0x1400077B0
//   usage                0x140009A00
//   per-computer dumper  0x140004D30  (OpenEventLog, read loop, format, optional clear)
//   message formatter    0x140006A20  (EventMessageFile DLLs, %%n parameters, FormatMessage)
//   final FormatMessage  0x140004B60  (FROM_HMODULE over each message module)
//   module loader        0x140008560  (LoadLibraryEx as datafile, cached)
//   registry string read 0x1400089D0  (RegQueryValueEx)
//   env expand / remap   0x140004AA0 / 0x140008BC0  (ExpandEnvironmentStrings, ADMIN$ rewrite)
//   source -o/-q filter  0x1400063A0
//   SID to account       0x140008670  (LookupAccountSid)
//   clear after display  0x140006300  (ClearEventLog)
//   open remote registry 0x140003CE0  (LogonUser, RegConnectRegistry)
//   print system error   0x140003FC0  (FormatMessage, netmsg.dll for net codes)
//
// The shared PsTools front end (banner, EULA gate, alternate credentials, system
// error printer, local identity, hidden password prompt, target iteration) lives
// in ../common/PsTools.h and is reused read-only. PsLogList parses its own command
// line because its grammar is far richer than the shared account-list parser.
//
// The record is the classic EVENTLOGRECORD. Message resolution follows the Windows
// event-log convention: open HKLM\SYSTEM\CurrentControlSet\Services\EventLog\<Log>\
// <Source>, read EventMessageFile (a semicolon list of DLLs), expand environment
// variables, LoadLibraryEx each as a data file, and FormatMessage the full 32-bit
// EventID over them with the record's insertion strings as the argument array.
// Any %%n reference inside an insertion string is resolved against ParameterMessageFile.

#include "../common/PsTools.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Version-resource fields of psloglist64.exe v2.82, used for the banner and the
    // EULA registry key. The original reads these from its own version resource at
    // run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsLoglist",
        "2.82",
        "local and remote event log viewer",
        "Copyright (C) 2000-2023 Mark Russinovich",
        "Sysinternals - www.sysinternals.com",
    };

    // The EULA app name is the version-resource InternalName, matching
    // HKCU\Software\Sysinternals\PsLoglist.
    constexpr char kApp[] = "PsLoglist";

    constexpr DWORD kInitialReadBuffer = 0x4000;
    constexpr int kMaxIdList = 10;          // -i / -e accept up to ten ids
    constexpr DWORD kFormatBuffer = 260;    // GetDateFormat / GetTimeFormat buffer
    constexpr int kHexBytesPerLine = 16;    // -x data dump width

    // Seconds between the 1601 FILETIME epoch and the 1970 Unix epoch, and the
    // 100ns ticks per second, used to turn an event record's time_t into a FILETIME.
    constexpr unsigned long long kUnixEpochInFiletimeSeconds = 11644473600ULL;
    constexpr unsigned long long kTicksPerSecond = 10000000ULL;
    constexpr long long kTicksPerDay = 864000000000LL;
    constexpr long long kTicksPerHour = 36000000000LL;
    constexpr long long kTicksPerMinute = 600000000LL;

    // --- RAII guards -------------------------------------------------------

    class EventLogGuard
    {
    public:
        explicit EventLogGuard(HANDLE h)
            : handle_(h)
        {
        }

        ~EventLogGuard()
        {
            if (handle_ != nullptr)
            {
                ::CloseEventLog(handle_);
            }
        }

        EventLogGuard(const EventLogGuard&) = delete;
        EventLogGuard& operator=(const EventLogGuard&) = delete;

        [[nodiscard]] HANDLE Get() const
        {
            return handle_;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    class RegKeyGuard
    {
    public:
        RegKeyGuard() = default;

        explicit RegKeyGuard(HKEY k)
            : key_(k)
        {
        }

        ~RegKeyGuard()
        {
            if (key_ != nullptr)
            {
                ::RegCloseKey(key_);
            }
        }

        RegKeyGuard(const RegKeyGuard&) = delete;
        RegKeyGuard& operator=(const RegKeyGuard&) = delete;

        [[nodiscard]] HKEY Get() const
        {
            return key_;
        }

        [[nodiscard]] PHKEY Receive()
        {
            return &key_;
        }

    private:
        HKEY key_ = nullptr;
    };

    // --- Parsed options ----------------------------------------------------

    struct Options
    {
        std::string logName = "System";    // the account argument, default System
        std::string computerList;          // \\computer, comma list, or @file
        std::string user;                  // -u value
        std::string password;              // -p value (or prompted)
        bool haveUser = false;
        bool havePassword = false;
        bool computerSpecified = false;

        bool oneLine = false;              // -s
        char delimiter = ',';              // -t
        bool haveDelim = false;

        int maxRecords = -1;               // -n
        bool oldestFirst = false;          // -r

        bool extended = false;             // -x
        bool clear = false;                // -c

        std::vector<int> includeIds;       // -i
        std::vector<int> excludeIds;       // -e

        std::string sourceInclude;         // -o
        bool haveSourceInclude = false;
        std::string sourceExclude;         // -q
        bool haveSourceExclude = false;

        std::string typeFilter;            // -f, uppercased

        bool cutoffSet = false;            // -d / -h / -m produce a single cutoff
        FILETIME cutoff = {};
        bool afterSet = false;             // -a
        FILETIME after = {};
        bool beforeSet = false;            // -b
        FILETIME before = {};
    };

    // --- Small text helpers ------------------------------------------------

    [[nodiscard]] bool IsAllDigits(const char* s)
    {
        if (s[0] == '\0')
        {
            return false;
        }
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (std::isdigit(static_cast<unsigned char>(*p)) == 0)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsDigitsAndCommas(const char* s)
    {
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (std::isdigit(static_cast<unsigned char>(*p)) == 0 && *p != ',')
            {
                return false;
            }
        }
        return true;
    }

    // Parse a comma-separated id list into out, capped at kMaxIdList entries, which
    // mirrors the original's ten-field sscanf.
    void ParseIdList(const char* s, std::vector<int>& out)
    {
        const char* p = s;
        while (*p != '\0' && static_cast<int>(out.size()) < kMaxIdList)
        {
            char* end = nullptr;
            const long value = std::strtol(p, &end, 10);
            if (end == p)
            {
                break;
            }
            out.push_back(static_cast<int>(value));
            p = end;
            if (*p == ',')
            {
                ++p;
            }
        }
    }

    // Canonicalize the log name the way the parser does: a case-insensitive prefix
    // of application, system, or security maps to the proper-cased built-in name,
    // otherwise the argument is kept as given.
    [[nodiscard]] std::string CanonicalLogName(const char* a)
    {
        const size_t n = std::strlen(a);
        if (n != 0 && ::_strnicmp(a, "application", n) == 0)
        {
            return "Application";
        }
        if (n != 0 && ::_strnicmp(a, "system", n) == 0)
        {
            return "System";
        }
        if (n != 0 && ::_strnicmp(a, "security", n) == 0)
        {
            return "Security";
        }
        return a;
    }

    [[nodiscard]] std::wstring Widen(const std::string& s)
    {
        if (s.empty())
        {
            return std::wstring();
        }
        const int needed = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        if (needed <= 0)
        {
            return std::wstring();
        }
        std::wstring result(static_cast<size_t>(needed), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, result.data(), needed);
        if (!result.empty() && result.back() == L'\0')
        {
            result.pop_back();
        }
        return result;
    }

    // Case-insensitive substring test, standing in for the original's sub_1400017C0.
    [[nodiscard]] bool ContainsNoCase(const char* haystack, const std::string& needle)
    {
        if (needle.empty())
        {
            return true;
        }
        const size_t nlen = needle.size();
        for (const char* p = haystack; *p != '\0'; ++p)
        {
            if (::_strnicmp(p, needle.c_str(), nlen) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // --- Event record decode -----------------------------------------------

    [[nodiscard]] const char* TypeString(WORD eventType)
    {
        if ((eventType & EVENTLOG_ERROR_TYPE) != 0)
        {
            return "ERROR";
        }
        if ((eventType & EVENTLOG_WARNING_TYPE) != 0)
        {
            return "WARNING";
        }
        if ((eventType & EVENTLOG_INFORMATION_TYPE) != 0)
        {
            return "INFORMATION";
        }
        if ((eventType & EVENTLOG_AUDIT_SUCCESS) != 0)
        {
            return "SUCCESS AUDIT";
        }
        if ((eventType & EVENTLOG_AUDIT_FAILURE) != 0)
        {
            return "FAILURE AUDIT";
        }
        return "UNKNOWN TYPE";
    }

    [[nodiscard]] FILETIME RecordLocalFileTime(DWORD timeGenerated)
    {
        const unsigned long long ticks =
            (static_cast<unsigned long long>(timeGenerated) + kUnixEpochInFiletimeSeconds)
            * kTicksPerSecond;
        FILETIME utc = {};
        utc.dwLowDateTime = static_cast<DWORD>(ticks);
        utc.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        FILETIME local = {};
        ::FileTimeToLocalFileTime(&utc, &local);
        return local;
    }

    void FormatRecordDateTime(DWORD timeGenerated, char* dateStr, char* timeStr)
    {
        const FILETIME local = RecordLocalFileTime(timeGenerated);
        SYSTEMTIME systemTime = {};
        ::FileTimeToSystemTime(&local, &systemTime);
        ::GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &systemTime, nullptr, timeStr, kFormatBuffer);
        ::GetDateFormatA(LOCALE_USER_DEFAULT, 0, &systemTime, nullptr, dateStr, kFormatBuffer);
    }

    [[nodiscard]] const char* RecordSource(const EVENTLOGRECORD* rec)
    {
        return reinterpret_cast<const char*>(rec) + sizeof(EVENTLOGRECORD);
    }

    [[nodiscard]] const char* RecordComputer(const EVENTLOGRECORD* rec)
    {
        const char* source = RecordSource(rec);
        return source + std::strlen(source) + 1;
    }

    // --- Message DLL resolution --------------------------------------------

    // Read a raw string value. The caller expands environment variables, matching
    // the split between sub_1400089D0 and sub_140004AA0 in the original.
    [[nodiscard]] std::string ReadRegString(HKEY key, const char* valueName)
    {
        if (key == nullptr)
        {
            return std::string();
        }
        DWORD size = 0;
        if (::RegQueryValueExA(key, valueName, nullptr, nullptr, nullptr, &size) != ERROR_SUCCESS
            || size == 0)
        {
            return std::string();
        }
        std::vector<char> buffer(size + 1, '\0');
        if (::RegQueryValueExA(key, valueName, nullptr, nullptr,
                               reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS)
        {
            return std::string();
        }
        return std::string(buffer.data());
    }

    [[nodiscard]] std::string ExpandPath(const std::string& token)
    {
        // The WinMgmtr.Dll special case the original handles in sub_140004AA0.
        if (::_stricmp(token.c_str(), "WinMgmtr.Dll") == 0)
        {
            char systemDir[MAX_PATH] = {};
            const UINT n = ::GetSystemDirectoryA(systemDir, static_cast<UINT>(std::size(systemDir)));
            if (n != 0 && n < std::size(systemDir))
            {
                return std::string(systemDir) + "\\Wbem\\WinMgmtr.Dll";
            }
        }
        const DWORD needed = ::ExpandEnvironmentStringsA(token.c_str(), nullptr, 0);
        if (needed == 0)
        {
            return token;
        }
        std::vector<char> buffer(needed, '\0');
        const DWORD written = ::ExpandEnvironmentStringsA(token.c_str(), buffer.data(), needed);
        if (written == 0 || written > needed)
        {
            return token;
        }
        return std::string(buffer.data());
    }

    // ponytail: the original keeps a global module cache (sub_140008560) so a DLL is
    // mapped once across every record. A process-lifetime cache here does the same.
    [[nodiscard]] HMODULE LoadMessageModule(const std::string& path)
    {
        static std::vector<std::pair<std::string, HMODULE>> cache;
        for (const auto& entry : cache)
        {
            if (::_stricmp(entry.first.c_str(), path.c_str()) == 0)
            {
                return entry.second;
            }
        }
        const HMODULE module = ::LoadLibraryExA(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (module != nullptr)
        {
            cache.emplace_back(path, module);
        }
        return module;
    }

    // Split a semicolon-separated EventMessageFile/ParameterMessageFile value into
    // expanded, loaded module handles.
    [[nodiscard]] std::vector<HMODULE> LoadModuleList(const std::string& value)
    {
        std::vector<HMODULE> modules;
        size_t start = 0;
        while (start <= value.size())
        {
            const size_t semi = value.find(';', start);
            const std::string token =
                value.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
            if (!token.empty())
            {
                const HMODULE module = LoadMessageModule(ExpandPath(token));
                modules.push_back(module);
            }
            if (semi == std::string::npos)
            {
                break;
            }
            start = semi + 1;
        }
        return modules;
    }

    // Format one message id against the first module that resolves it, returning a
    // LocalAlloc buffer or nullptr. Parameter messages pass no inserts.
    [[nodiscard]] char* FormatFromModules(const std::vector<HMODULE>& modules, DWORD messageId,
                                          DWORD flags, DWORD_PTR* args)
    {
        for (const HMODULE module : modules)
        {
            if (module == nullptr)
            {
                continue;
            }
            char* buffer = nullptr;
            const DWORD count = ::FormatMessageA(flags, module, messageId, 0,
                                                 reinterpret_cast<LPSTR>(&buffer), 0,
                                                 reinterpret_cast<va_list*>(args));
            if (count != 0 && buffer != nullptr)
            {
                return buffer;
            }
            if (buffer != nullptr)
            {
                ::LocalFree(buffer);
            }
        }
        return nullptr;
    }

    // Replace every %%n reference in an insertion string with the parameter message
    // text, mirroring the inner loop of sub_140006A20.
    [[nodiscard]] std::string ExpandParameters(const char* text, const std::vector<HMODULE>& paramModules)
    {
        constexpr DWORD kFromModuleAllocate = FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER;
        std::string out;
        const char* p = text;
        for (;;)
        {
            const char* marker = std::strstr(p, "%%");
            if (marker == nullptr)
            {
                out.append(p);
                break;
            }
            out.append(p, marker);
            const char* digits = marker + 2;
            DWORD id = 0;
            bool any = false;
            while (*digits >= '0' && *digits <= '9')
            {
                id = id * 10 + static_cast<DWORD>(*digits - '0');
                ++digits;
                any = true;
            }
            char* resolved = any ? FormatFromModules(paramModules, id & 0xFFFF, kFromModuleAllocate, nullptr)
                                 : nullptr;
            if (resolved != nullptr)
            {
                out.append(resolved);
                ::LocalFree(resolved);
            }
            else
            {
                out.append(marker, digits);
            }
            p = digits;
        }
        return out;
    }

    // Collapse the message's embedded line breaks the way sub_140006A20 does before
    // return. In one-line mode tab, CR, and LF all become spaces. In full mode a
    // lone LF that is not part of a CRLF pair becomes a space.
    void NormalizeMessage(std::string& msg, bool oneLine)
    {
        const size_t len = msg.size();
        if (oneLine)
        {
            for (char& c : msg)
            {
                if (c == '\t' || c == '\n' || c == '\r')
                {
                    c = ' ';
                }
            }
            return;
        }
        for (size_t i = 0; i < len; ++i)
        {
            const char c = msg[i];
            const bool prevCr = i >= 1 && msg[i - 1] == '\r';
            const bool prev2Cr = i >= 2 && msg[i - 2] == '\r';
            const bool nextCr = i + 1 < len && msg[i + 1] == '\r';
            if (c == '\r' && prev2Cr)
            {
                if (i + 1 < len)
                {
                    msg[i + 1] = ' ';
                }
                msg[i] = ' ';
            }
            else if (c == '\n' && !nextCr && !prevCr)
            {
                msg[i] = ' ';
            }
        }
    }

    // Resolve the record's message. Sets available to whether a message DLL produced
    // text. When it did not, the returned string is the insertion strings joined with
    // spaces, which the caller presents under "Insertion strings".
    [[nodiscard]] std::string FormatEventMessage(HKEY logKey, HKEY fallbackKey,
                                                 const EVENTLOGRECORD* rec, bool oneLine,
                                                 bool& available)
    {
        constexpr DWORD kEventFlags =
            FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_ARGUMENT_ARRAY;
        available = false;

        const char* source = RecordSource(rec);
        RegKeyGuard sourceKey;
        ::RegOpenKeyA(logKey, source, sourceKey.Receive());

        std::string messageFiles = ReadRegString(sourceKey.Get(), "EventMessageFile");
        if (messageFiles.empty())
        {
            messageFiles = ReadRegString(fallbackKey, "EventMessageFile");
        }
        std::string parameterFiles = ReadRegString(sourceKey.Get(), "ParameterMessageFile");
        if (parameterFiles.empty())
        {
            parameterFiles = ReadRegString(fallbackKey, "ParameterMessageFile");
        }

        const std::vector<HMODULE> messageModules = LoadModuleList(messageFiles);
        const std::vector<HMODULE> parameterModules = LoadModuleList(parameterFiles);

        const WORD numStrings = rec->NumStrings;
        std::vector<const char*> rawStrings;
        rawStrings.reserve(numStrings);
        {
            const char* s = reinterpret_cast<const char*>(rec) + rec->StringOffset;
            for (WORD i = 0; i < numStrings; ++i)
            {
                rawStrings.push_back(s);
                s += std::strlen(s) + 1;
            }
        }

        std::vector<std::string> expandedStore(numStrings);
        std::vector<DWORD_PTR> args(numStrings == 0 ? 1 : numStrings, 0);
        for (WORD i = 0; i < numStrings; ++i)
        {
            if (std::strstr(rawStrings[i], "%%") != nullptr)
            {
                expandedStore[i] = ExpandParameters(rawStrings[i], parameterModules);
                args[i] = reinterpret_cast<DWORD_PTR>(expandedStore[i].c_str());
            }
            else
            {
                args[i] = reinterpret_cast<DWORD_PTR>(rawStrings[i]);
            }
        }

        std::string result;
        char* formatted = FormatFromModules(messageModules, rec->EventID, kEventFlags, args.data());
        if (formatted != nullptr)
        {
            available = true;
            result.assign(formatted);
            ::LocalFree(formatted);
        }
        else
        {
            for (WORD i = 0; i < numStrings; ++i)
            {
                result.append(reinterpret_cast<const char*>(args[i]));
                result.push_back(' ');
            }
            // The original closes the joined insertion strings with a newline
            // (sub_140006A20), which NormalizeMessage then folds into a space.
            result.push_back('\n');
        }

        NormalizeMessage(result, oneLine);
        return result;
    }

    // --- SID resolution ----------------------------------------------------

    // Resolve the record's user SID to its account name and domain. Mirrors
    // sub_140008670: the name field comes first, the domain second, which is the
    // order the original then prints (name backslash domain).
    [[nodiscard]] bool ResolveSid(const char* machine, PSID sid, std::string& name, std::string& domain)
    {
        DWORD nameLen = 0;
        DWORD domainLen = 0;
        SID_NAME_USE use = SidTypeUnknown;
        if (::LookupAccountSidA(machine, sid, nullptr, &nameLen, nullptr, &domainLen, &use)
            || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            return false;
        }
        std::vector<char> nameBuffer(nameLen, '\0');
        std::vector<char> domainBuffer(domainLen, '\0');
        if (!::LookupAccountSidA(machine, sid, nameBuffer.data(), &nameLen,
                                 domainBuffer.data(), &domainLen, &use))
        {
            return false;
        }
        name.assign(nameBuffer.data());
        domain.assign(domainBuffer.data());
        return true;
    }

    // --- Source -o / -q filter ---------------------------------------------

    // Decide whether a record passes the source filter, mirroring sub_1400063A0.
    // -o keeps a record whose source matches one of the listed sources, -q drops it.
    // A token ending in '*' matches as a substring of the source name.
    [[nodiscard]] bool SourceMatches(const char* source, const Options& o)
    {
        const std::string* list = nullptr;
        bool negate = false;
        if (o.haveSourceInclude)
        {
            list = &o.sourceInclude;
            negate = false;
        }
        else if (o.haveSourceExclude)
        {
            list = &o.sourceExclude;
            negate = true;
        }
        else
        {
            return true;
        }

        bool matched = false;
        size_t start = 0;
        while (start <= list->size() && !matched)
        {
            const size_t comma = list->find(',', start);
            const std::string token =
                list->substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            start = comma == std::string::npos ? list->size() + 1 : comma + 1;
            if (token.empty())
            {
                continue;
            }
            const size_t star = token.find('*');
            if (star == std::string::npos)
            {
                if (::_stricmp(source, token.c_str()) == 0)
                {
                    matched = true;
                }
            }
            else if (ContainsNoCase(source, token.substr(0, star)))
            {
                matched = true;
            }
        }
        return negate ? !matched : matched;
    }

    // --- Record selection --------------------------------------------------

    [[nodiscard]] bool PassesFilters(const EVENTLOGRECORD* rec, const Options& o)
    {
        if (o.afterSet || o.beforeSet || o.cutoffSet)
        {
            const FILETIME recordTime = RecordLocalFileTime(rec->TimeGenerated);
            if (o.afterSet && ::CompareFileTime(&recordTime, &o.after) < 0)
            {
                return false;
            }
            if (o.beforeSet && ::CompareFileTime(&recordTime, &o.before) > 0)
            {
                return false;
            }
            if (o.cutoffSet && ::CompareFileTime(&recordTime, &o.cutoff) < 0)
            {
                return false;
            }
        }

        if (!o.typeFilter.empty())
        {
            const char first = TypeString(rec->EventType)[0];
            if (o.typeFilter.find(first) == std::string::npos)
            {
                return false;
            }
        }

        const int id = static_cast<int>(rec->EventID & 0xFFFF);
        if (!o.includeIds.empty())
        {
            if (std::find(o.includeIds.begin(), o.includeIds.end(), id) == o.includeIds.end())
            {
                return false;
            }
        }
        else if (!o.excludeIds.empty())
        {
            if (std::find(o.excludeIds.begin(), o.excludeIds.end(), id) != o.excludeIds.end())
            {
                return false;
            }
        }

        return SourceMatches(RecordSource(rec), o);
    }

    // --- Record output -----------------------------------------------------

    void PrintRecord(const EVENTLOGRECORD* rec, const Options& o, const char* machine,
                     HKEY logKey, HKEY fallbackKey)
    {
        const char* source = RecordSource(rec);
        const char* computer = RecordComputer(rec);
        const char* typeStr = TypeString(rec->EventType);
        const int eventId = static_cast<int>(rec->EventID & 0xFFFF);
        const int recordNumber = static_cast<int>(rec->RecordNumber);

        char dateStr[kFormatBuffer] = {};
        char timeStr[kFormatBuffer] = {};
        FormatRecordDateTime(rec->TimeGenerated, dateStr, timeStr);

        bool available = false;
        const std::string message = FormatEventMessage(logKey, fallbackKey, rec, o.oneLine, available);

        std::string name;
        std::string domain;
        bool haveAccount = false;
        if (rec->UserSidLength != 0)
        {
            PSID sid = reinterpret_cast<PSID>(
                reinterpret_cast<BYTE*>(const_cast<EVENTLOGRECORD*>(rec)) + rec->UserSidOffset);
            haveAccount = ResolveSid(machine, sid, name, domain);
        }

        if (o.oneLine)
        {
            const char d = o.delimiter;
            std::printf("%03d%c%s%c%s%c", recordNumber, d, o.logName.c_str(), d, source, d);
            std::printf("%s%c", typeStr, d);
            std::printf("%s%c", computer, d);
            std::printf("%s %s%c%d%c", dateStr, timeStr, d, eventId, d);
            if (rec->UserSidLength != 0 && haveAccount)
            {
                std::printf("%s\\%s%c", name.c_str(), domain.c_str(), d);
            }
            else
            {
                std::printf("None%c", d);
            }
            std::printf("\"");
            if (!available)
            {
                std::printf("Message text not available.");
                if (rec->NumStrings != 0)
                {
                    std::printf("  Insertion strings: ");
                }
            }
            std::printf("%s\"\n", message.c_str());
            return;
        }

        std::printf("[%03d] %s\n", recordNumber, source);
        std::printf("   Type:     ");
        std::printf("%s ", typeStr);
        std::printf("\n");
        std::printf("   Computer: %s\n", computer);
        std::printf("   Time:     %s %s", dateStr, timeStr);
        std::printf("   ID:       %d \n", eventId);
        if (rec->UserSidLength != 0 && haveAccount)
        {
            std::printf("   User:     %s\\%s\n", name.c_str(), domain.c_str());
        }
        if (available)
        {
            std::printf("%s\n", message.c_str());
        }
        else
        {
            std::printf("Message text not available.");
            if (rec->NumStrings != 0)
            {
                std::printf("  Insertion strings:\n\t%s", message.c_str());
            }
            std::printf("\n\n");
        }

        if (rec->DataLength != 0 && o.extended)
        {
            std::printf("   Data:\n");
            const BYTE* data = reinterpret_cast<const BYTE*>(rec) + rec->DataOffset;
            const DWORD length = rec->DataLength;
            for (DWORD offset = 0; offset < length;)
            {
                DWORD chunk = kHexBytesPerLine;
                if (offset + kHexBytesPerLine > length)
                {
                    chunk = length - offset;
                }
                std::printf("   %04X: ", offset);
                for (DWORD i = 0; i < chunk; ++i)
                {
                    std::printf("%02X ", data[offset + i]);
                }
                for (DWORD i = chunk; i < kHexBytesPerLine; ++i)
                {
                    std::printf("   ");
                }
                for (DWORD i = 0; i < chunk; ++i)
                {
                    const unsigned char byte = data[offset + i];
                    const int shown = std::isprint(byte) != 0 ? byte : '.';
                    std::printf("%c", shown);
                }
                std::printf("\n");
                offset += chunk;
            }
            std::printf("\n");
        }
    }

    // --- Clear (never exercised in validation) -----------------------------

    // Mirrors sub_140006300. The project never clears a log during validation.
    void ClearLog(HANDLE hEventLog, const char* machine, const std::string& logName)
    {
        if (::ClearEventLogA(hEventLog, nullptr))
        {
            std::printf("%s event log on %s cleared.\n", logName.c_str(), machine);
        }
        else
        {
            std::fprintf(stderr, "Error clearing %s event log on %s.\n", logName.c_str(), machine);
        }
    }

    // --- Per-computer dumper ----------------------------------------------

    [[nodiscard]] bool DumpEventLog(const char* machine, const Options& o)
    {
        const std::string local = pstools::LocalComputerName();
        const bool remote = machine[0] != '\0' && ::_stricmp(machine, local.c_str()) != 0;

        if (!o.oneLine)
        {
            std::printf("%s log on \\\\%s:\n", o.logName.c_str(), machine);
        }
        std::fflush(stdout);

        bool connected = false;
        if (remote && o.haveUser)
        {
            connected = pstools::RemoteConnect(machine, o.user.c_str(), o.password.c_str());
        }

        const std::wstring wideMachine = Widen(machine);
        std::string leafLog = o.logName;
        const size_t slash = leafLog.find_last_of('\\');
        if (slash != std::string::npos)
        {
            leafLog = leafLog.substr(slash + 1);
        }
        const EventLogGuard eventLog(::OpenEventLogW(wideMachine.c_str(), Widen(leafLog).c_str()));
        if (eventLog.Get() == nullptr)
        {
            std::fprintf(stderr, "Could not open %s event log on %s:\n", o.logName.c_str(), machine);
            pstools::PrintSystemError(::GetLastError());
            if (connected)
            {
                pstools::RemoteDisconnect(machine);
            }
            return false;
        }

        RegKeyGuard connectedRoot;
        HKEY root = HKEY_LOCAL_MACHINE;
        if (remote && ::RegConnectRegistryA(machine, HKEY_LOCAL_MACHINE, connectedRoot.Receive())
                          == ERROR_SUCCESS)
        {
            root = connectedRoot.Get();
        }

        const std::string subKey = "System\\CurrentControlSet\\Services\\EventLog\\" + o.logName;
        RegKeyGuard logKey;
        const LSTATUS openStatus = ::RegOpenKeyA(root, subKey.c_str(), logKey.Receive());
        if (openStatus != ERROR_SUCCESS)
        {
            std::fprintf(stderr, "Could not open the specified event log Registry key on %s:\n", machine);
            pstools::PrintSystemError(static_cast<DWORD>(openStatus));
            if (connected)
            {
                pstools::RemoteDisconnect(machine);
            }
            return false;
        }

        // The fallback key is the PrimaryModule subkey when the log declares one,
        // otherwise the log key itself, matching sub_140004D30.
        RegKeyGuard primaryKey;
        HKEY fallbackKey = logKey.Get();
        const std::string primary = ReadRegString(logKey.Get(), "PrimaryModule");
        if (!primary.empty()
            && ::RegOpenKeyA(logKey.Get(), primary.c_str(), primaryKey.Receive()) == ERROR_SUCCESS)
        {
            fallbackKey = primaryKey.Get();
        }

        DWORD bufferSize = kInitialReadBuffer;
        std::vector<BYTE> buffer(bufferSize);
        const DWORD readFlags =
            EVENTLOG_SEQUENTIAL_READ | (o.oldestFirst ? EVENTLOG_FORWARDS_READ : EVENTLOG_BACKWARDS_READ);
        DWORD lastRecord = o.oldestFirst ? 0u : 0xFFFFFFFFu;
        int printed = 0;
        bool stop = false;

        while (!stop)
        {
            DWORD read = 0;
            DWORD needed = 0;
            if (!::ReadEventLogA(eventLog.Get(), readFlags, 0, buffer.data(), bufferSize, &read, &needed))
            {
                const DWORD error = ::GetLastError();
                if (error == ERROR_INSUFFICIENT_BUFFER)
                {
                    bufferSize = needed;
                    buffer.resize(bufferSize);
                    continue;
                }
                if (error == ERROR_HANDLE_EOF)
                {
                    if (printed == 0)
                    {
                        std::fprintf(stderr, "No records in %s event log on %s.\n",
                                     o.logName.c_str(), machine);
                    }
                    break;
                }
                std::fprintf(stderr, "Error reading %s event log on %s:\n", o.logName.c_str(), machine);
                pstools::PrintSystemError(error);
                break;
            }
            if (read == 0)
            {
                break;
            }

            DWORD offset = 0;
            while (offset < read)
            {
                auto* rec = reinterpret_cast<EVENTLOGRECORD*>(buffer.data() + offset);
                const DWORD recordNumber = rec->RecordNumber;
                if (o.oldestFirst)
                {
                    if (recordNumber < lastRecord)
                    {
                        stop = true;
                        break;
                    }
                }
                else if (recordNumber > lastRecord)
                {
                    stop = true;
                    break;
                }
                lastRecord = recordNumber;

                if (PassesFilters(rec, o))
                {
                    PrintRecord(rec, o, machine, logKey.Get(), fallbackKey);
                    ++printed;
                    if (o.maxRecords == printed)
                    {
                        stop = true;
                        break;
                    }
                }
                offset += rec->Length;
            }
        }

        if (o.clear)
        {
            ClearLog(eventLog.Get(), machine, o.logName);
        }
        if (connected)
        {
            pstools::RemoteDisconnect(machine);
        }
        return true;
    }

    // --- Command line ------------------------------------------------------

    void PrintUsage()
    {
        std::printf("PsLogList dumps event logs on a local or remote NT system.\n");
        std::printf("\nUsage: psloglist [\\\\computer[,computer2[,...] | @file] [-u username [-p password]]]"
                    " [-s [-t delimiter]] [-m #|-n #|-d #|-h #|-w][-c][-x][-r][-a mm/dd/yy][-b mm/dd/yy]"
                    " [-f filter] [-i ID,[ID,...]] | -e ID,[ID,...]] [-o event source[,event source[,...]]]"
                    " [-q event source[,event source[,...]]] [[-g|-l] event log file] <event log>\n");
        std::printf("     @file     Psloglist will execute the command on each of the computers\n");
        std::printf("               listed in the file.\n");
        std::printf("     -a        Dump records timestamped after specified date.\n");
        std::printf("     -b        Dump records timestamped before specified date.\n");
        std::printf("     -c        Clear event log after displaying.\n");
        std::printf("     -d        Only display records from previous n days.\n");
        std::printf("     -e        Exclude events with the specified ID or IDs (up to 10).\n");
        std::printf("     -f        Filter event types, using starting letter\n");
        std::printf("               (e.g. \"-f we\" to filter warnings and errors).\n");
        std::printf("     -g        Export an event log as an evt file.\n");
        std::printf("     -h        Only display records from previous n hours.\n");
        std::printf("     -i        Show only events with the specified ID or IDs (up to 10).\n");
        std::printf("     -l        Dump the contents of the specified saved event log file.\n");
        std::printf("     -m        Only display records from previous n minutes.\n");
        std::printf("     -n        Only display n most recent records.\n");
        std::printf("     -o        Show only records from the specified event source or sources\n");
        std::printf("               (e.g. \"-o cdrom\"). Append '*' to specify substring match.\n");
        std::printf("     -p        Specifies password for user name.\n");
        std::printf("     -q        Omit records from the specified event source or sources\n");
        std::printf("               (e.g. \"-q cdrom\").\n");
        std::printf("               Append '*' to specify substring match.\n");
        std::printf("     -r        Dump log from least recent to most recent.\n");
        std::printf("     -s        Records are listed on one line each with delimited\n"
                    "               fields, which is convenient for string searches.\n");
        std::printf("     -t        The default delimiter for the -s option is a comma,\n"
                    "               but can be overriden with the specified character. Use \"\\t\"\n"
                    "               to specify tab.\n");
        std::printf("     -u        Specifies optional user name for login to\n               remote computer.\n");
        std::printf("     -w        Wait for new events, dumping them as they generate (local system\n");
        std::printf("               only.)\n");
        std::printf("     -x        Dump extended data.\n");
        std::printf("     -z        List event logs registered on specified system.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("     eventlog  Specifies event log to dump. Default is system. If the\n"
                    "               -l switch is present then the event log name specifies\n"
                    "               how to interpret the event log file.\n");
        std::printf("\n");
    }

    // Compute the -d / -h / -m cutoff FILETIME from "now", matching the parser.
    [[nodiscard]] FILETIME ComputeCutoff(long long ticksBack, bool truncateToMidnight)
    {
        FILETIME now = {};
        ::GetSystemTimeAsFileTime(&now);
        FILETIME local = {};
        ::FileTimeToLocalFileTime(&now, &local);
        unsigned long long value =
            (static_cast<unsigned long long>(local.dwHighDateTime) << 32) | local.dwLowDateTime;
        value -= static_cast<unsigned long long>(ticksBack);
        FILETIME cutoff = {};
        cutoff.dwLowDateTime = static_cast<DWORD>(value);
        cutoff.dwHighDateTime = static_cast<DWORD>(value >> 32);
        if (truncateToMidnight)
        {
            SYSTEMTIME systemTime = {};
            ::FileTimeToSystemTime(&cutoff, &systemTime);
            systemTime.wHour = 0;
            systemTime.wMinute = 0;
            systemTime.wSecond = 0;
            systemTime.wMilliseconds = 0;
            ::SystemTimeToFileTime(&systemTime, &cutoff);
        }
        return cutoff;
    }

    // Parse an mm/dd/yy date into a local FILETIME, matching the parser's year rule.
    [[nodiscard]] bool ParseDate(const char* text, FILETIME& out)
    {
        int month = 0;
        int day = 0;
        int year = 0;
        if (std::sscanf(text, "%d/%d/%d", &month, &day, &year) != 3)
        {
            return false;
        }
        if (year >= 90)
        {
            if (static_cast<unsigned>(year - 91) <= 8)
            {
                year += 1900;
            }
        }
        else
        {
            year += 2000;
        }
        SYSTEMTIME systemTime = {};
        systemTime.wMonth = static_cast<WORD>(month);
        systemTime.wDay = static_cast<WORD>(day);
        systemTime.wYear = static_cast<WORD>(year);
        return ::SystemTimeToFileTime(&systemTime, &out) != 0;
    }

    // Parse the PsLogList command line. Mirrors sub_1400077B0 for the record-read,
    // message-format, and record-selection switches. The event-file, wait, and
    // list-logs modes (-g, -l, -w, -z) are out of scope and reported through usage.
    [[nodiscard]] bool ParseCommandLine(int argc, char** argv, Options& o)
    {
        bool haveComputer = false;
        bool haveLog = false;

        for (int i = 1; i < argc; ++i)
        {
            const char* arg = argv[i];
            if (arg[0] == '-' || arg[0] == '/')
            {
                if (arg[1] == '\0')
                {
                    continue;
                }
                bool consumedNext = false;
                for (int j = 1; arg[j] != '\0' && !consumedNext; ++j)
                {
                    const int c = std::toupper(static_cast<unsigned char>(arg[j]));
                    switch (c)
                    {
                    case 'S':
                        if (o.oneLine)
                        {
                            return false;
                        }
                        o.oneLine = true;
                        break;
                    case 'R':
                        o.oldestFirst = true;
                        break;
                    case 'C':
                        o.clear = true;
                        break;
                    case 'X':
                        o.extended = true;
                        break;
                    case 'T':
                        if (!o.oneLine || o.haveDelim || i + 1 >= argc)
                        {
                            return false;
                        }
                        o.haveDelim = true;
                        if (::_stricmp(argv[i + 1], "\\t") == 0)
                        {
                            o.delimiter = '\t';
                        }
                        else
                        {
                            o.delimiter = argv[i + 1][0];
                        }
                        ++i;
                        consumedNext = true;
                        break;
                    case 'N':
                        if (i + 1 >= argc || o.maxRecords != -1 || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.maxRecords = std::atoi(argv[i + 1]);
                        ++i;
                        consumedNext = true;
                        break;
                    case 'D':
                        if (i + 1 >= argc || o.cutoffSet || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.cutoff = ComputeCutoff(kTicksPerDay * std::atoi(argv[i + 1]), true);
                        o.cutoffSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'H':
                        if (i + 1 >= argc || o.cutoffSet || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.cutoff = ComputeCutoff(kTicksPerHour * std::atoi(argv[i + 1]), false);
                        o.cutoffSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'M':
                        if (i + 1 >= argc || o.cutoffSet || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.cutoff = ComputeCutoff(kTicksPerMinute * std::atoi(argv[i + 1]), false);
                        o.cutoffSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'I':
                        if (i + 1 >= argc || !o.includeIds.empty() || !o.excludeIds.empty()
                            || !IsDigitsAndCommas(argv[i + 1]))
                        {
                            return false;
                        }
                        ParseIdList(argv[i + 1], o.includeIds);
                        ++i;
                        consumedNext = true;
                        break;
                    case 'E':
                        if (i + 1 >= argc || !o.includeIds.empty() || !o.excludeIds.empty()
                            || !IsDigitsAndCommas(argv[i + 1]))
                        {
                            return false;
                        }
                        ParseIdList(argv[i + 1], o.excludeIds);
                        ++i;
                        consumedNext = true;
                        break;
                    case 'O':
                        if (i + 1 >= argc || o.haveSourceInclude)
                        {
                            return false;
                        }
                        o.sourceInclude = argv[i + 1];
                        o.haveSourceInclude = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'Q':
                        if (i + 1 >= argc || o.haveSourceExclude)
                        {
                            return false;
                        }
                        o.sourceExclude = argv[i + 1];
                        o.haveSourceExclude = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'F':
                        if (i + 1 >= argc || !o.typeFilter.empty())
                        {
                            return false;
                        }
                        o.typeFilter = argv[i + 1];
                        std::transform(o.typeFilter.begin(), o.typeFilter.end(), o.typeFilter.begin(),
                                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                        ++i;
                        consumedNext = true;
                        break;
                    case 'A':
                        if (i + 1 >= argc || o.afterSet || !ParseDate(argv[i + 1], o.after))
                        {
                            return false;
                        }
                        o.afterSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'B':
                        if (i + 1 >= argc || o.beforeSet || !ParseDate(argv[i + 1], o.before))
                        {
                            return false;
                        }
                        o.beforeSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'U':
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[i + 1];
                        o.haveUser = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'P':
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[i + 1];
                        o.havePassword = true;
                        ++i;
                        consumedNext = true;
                        break;
                    default:
                        return false;
                    }
                }
            }
            else if (arg[0] == '@')
            {
                if (haveComputer)
                {
                    return false;
                }
                o.computerList = arg;
                haveComputer = true;
                o.computerSpecified = true;
            }
            else if (arg[0] == '\\' && arg[1] == '\\')
            {
                if (haveComputer)
                {
                    return false;
                }
                o.computerList = arg + 2;
                haveComputer = true;
                o.computerSpecified = true;
            }
            else
            {
                if (haveLog)
                {
                    return false;
                }
                o.logName = CanonicalLogName(arg);
                haveLog = true;
            }
        }

        if (o.haveUser && !o.computerSpecified)
        {
            return false;
        }
        if (o.havePassword && !o.haveUser)
        {
            return false;
        }
        if (o.afterSet && o.beforeSet && ::CompareFileTime(&o.after, &o.before) > 0)
        {
            return false;
        }
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
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    Options options;
    if (!ParseCommandLine(argc, argv, options))
    {
        PrintUsage();
        return -1;
    }

    if (options.haveUser && !options.havePassword)
    {
        options.password = pstools::PromptHiddenPassword();
        options.havePassword = true;
    }

    if (options.computerList.empty())
    {
        options.computerList = pstools::LocalComputerName();
    }

    const bool ok = pstools::ForEachComputer(options.computerList, true,
                                             [&options](const char* computer)
                                             {
                                                 return DumpEventLog(computer, options);
                                             });
    return ok ? 0 : 1;
}
