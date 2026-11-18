// PsLogList event-log reading engine: the source and record-selection filters, the
// log clear, and the per-computer read loop (mirrors sub_140004D30). See
// analysis/PsLogList.md for the sub_ map.
#pragma once

#include "EventMessage.h"

namespace psloglist
{
    // Decide whether a record passes the source filter, mirroring sub_1400063A0.
    // -o keeps a record whose source matches one of the listed sources, -q drops it.
    // A token ending in '*' matches as a substring of the source name.
    [[nodiscard]] inline bool SourceMatches(const char* source, const Options& o)
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

    [[nodiscard]] inline bool PassesFilters(const EVENTLOGRECORD* rec, const Options& o)
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

    // Mirrors sub_140006300. The project never clears a log during validation.
    inline void ClearLog(HANDLE hEventLog, const char* machine, const std::string& logName)
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

    [[nodiscard]] inline bool DumpEventLog(const char* machine, const Options& o)
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
        if (remote && ::RegConnectRegistryA(machine, HKEY_LOCAL_MACHINE, connectedRoot.Receive()) == ERROR_SUCCESS)
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
        if (!primary.empty() && ::RegOpenKeyA(logKey.Get(), primary.c_str(), primaryKey.Receive()) == ERROR_SUCCESS)
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
                        std::fprintf(stderr, "No records in %s event log on %s.\n", o.logName.c_str(), machine);
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
} // namespace psloglist
