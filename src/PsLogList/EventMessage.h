// PsLogList message formatting and record output: the record decode helpers, the
// message-DLL resolution, the SID lookup, and the one-record printer. See
// analysis/PsLogList.md for the sub_ map.
#pragma once

#include "LogListUtil.h"

namespace psloglist
{
    [[nodiscard]] inline const char* TypeString(WORD eventType)
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

    [[nodiscard]] inline FILETIME RecordLocalFileTime(DWORD timeGenerated)
    {
        const unsigned long long ticks =
            (static_cast<unsigned long long>(timeGenerated) + kUnixEpochInFiletimeSeconds) * kTicksPerSecond;
        FILETIME utc = {};
        utc.dwLowDateTime = static_cast<DWORD>(ticks);
        utc.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        FILETIME local = {};
        ::FileTimeToLocalFileTime(&utc, &local);
        return local;
    }

    inline void FormatRecordDateTime(DWORD timeGenerated, char* dateStr, char* timeStr)
    {
        const FILETIME local = RecordLocalFileTime(timeGenerated);
        SYSTEMTIME systemTime = {};
        ::FileTimeToSystemTime(&local, &systemTime);
        ::GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &systemTime, nullptr, timeStr, kFormatBuffer);
        ::GetDateFormatA(LOCALE_USER_DEFAULT, 0, &systemTime, nullptr, dateStr, kFormatBuffer);
    }

    [[nodiscard]] inline const char* RecordSource(const EVENTLOGRECORD* rec)
    {
        return reinterpret_cast<const char*>(rec) + sizeof(EVENTLOGRECORD);
    }

    [[nodiscard]] inline const char* RecordComputer(const EVENTLOGRECORD* rec)
    {
        const char* source = RecordSource(rec);
        return source + std::strlen(source) + 1;
    }

    // Read a raw string value. The caller expands environment variables, matching
    // the split between sub_1400089D0 and sub_140004AA0 in the original.
    [[nodiscard]] inline std::string ReadRegString(HKEY key, const char* valueName)
    {
        if (key == nullptr)
        {
            return std::string();
        }
        DWORD size = 0;
        if (::RegQueryValueExA(key, valueName, nullptr, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0)
        {
            return std::string();
        }
        std::vector<char> buffer(size + 1, '\0');
        if (::RegQueryValueExA(key, valueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &size) !=
            ERROR_SUCCESS)
        {
            return std::string();
        }
        return std::string(buffer.data());
    }

    [[nodiscard]] inline std::string ExpandPath(const std::string& token)
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
    [[nodiscard]] inline HMODULE LoadMessageModule(const std::string& path)
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
    [[nodiscard]] inline std::vector<HMODULE> LoadModuleList(const std::string& value)
    {
        std::vector<HMODULE> modules;
        size_t start = 0;
        while (start <= value.size())
        {
            const size_t semi = value.find(';', start);
            const std::string token = value.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
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
    [[nodiscard]] inline char* FormatFromModules(const std::vector<HMODULE>& modules, DWORD messageId, DWORD flags,
                                                 DWORD_PTR* args)
    {
        for (const HMODULE module : modules)
        {
            if (module == nullptr)
            {
                continue;
            }
            char* buffer = nullptr;
            const DWORD count = ::FormatMessageA(flags, module, messageId, 0, reinterpret_cast<LPSTR>(&buffer), 0,
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

    // Replace every %%n reference in an insertion string with the resolved parameter
    // message, mirroring the inner loop of sub_140006A20.
    [[nodiscard]] inline std::string ExpandParameters(const char* text, const std::vector<HMODULE>& paramModules)
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
            char* resolved = any ? FormatFromModules(paramModules, id & 0xFFFF, kFromModuleAllocate, nullptr) : nullptr;
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
    inline void NormalizeMessage(std::string& msg, bool oneLine)
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
    // output. When it did not, the returned string is the insertion strings joined
    // with spaces, which the caller presents under Insertion strings.
    [[nodiscard]] inline std::string FormatEventMessage(HKEY logKey, HKEY fallbackKey, const EVENTLOGRECORD* rec,
                                                        bool oneLine, bool& available)
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

    // Resolve the record's user SID to its account name and domain. Mirrors
    // sub_140008670: the name field comes first, the domain second, which is the
    // order the original then prints (name backslash domain).
    [[nodiscard]] inline bool ResolveSid(const char* machine, PSID sid, std::string& name, std::string& domain)
    {
        DWORD nameLen = 0;
        DWORD domainLen = 0;
        SID_NAME_USE use = SidTypeUnknown;
        if (::LookupAccountSidA(machine, sid, nullptr, &nameLen, nullptr, &domainLen, &use) ||
            ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            return false;
        }
        std::vector<char> nameBuffer(nameLen, '\0');
        std::vector<char> domainBuffer(domainLen, '\0');
        if (!::LookupAccountSidA(machine, sid, nameBuffer.data(), &nameLen, domainBuffer.data(), &domainLen, &use))
        {
            return false;
        }
        name.assign(nameBuffer.data());
        domain.assign(domainBuffer.data());
        return true;
    }

    inline void PrintRecord(const EVENTLOGRECORD* rec, const Options& o, const char* machine, HKEY logKey,
                            HKEY fallbackKey)
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
            PSID sid =
                reinterpret_cast<PSID>(reinterpret_cast<BYTE*>(const_cast<EVENTLOGRECORD*>(rec)) + rec->UserSidOffset);
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
} // namespace psloglist
