// PsSuspend result reporting: the success and failure summary lines for the local
// and remote paths, from the tail of main sub_140005B30. See analysis/PsSuspend.md.
#pragma once

#include "CommandLine.h"

namespace pssuspend
{
    inline void ReportLocalSuccess(const Options& o, int count)
    {
        const char* const word = o.resume ? "resumed" : "suspended";
        if (count > 1)
        {
            std::printf("%d processes named %s %s.\n\n", count, o.name.c_str(), word);
        }
        else if (o.byName)
        {
            std::printf("Process %s %s.\n\n", o.name.c_str(), word);
        }
        else
        {
            std::printf("Process %d %s.\n\n", o.pid, word);
        }
    }

    [[nodiscard]] inline int ReportLocalFailure(const Options& o, DWORD err)
    {
        const char* const verb = o.resume ? "resume" : "suspend";
        if (o.byName)
        {
            std::printf("Unable to %s process %s:\n", verb, o.name.c_str());
        }
        else
        {
            std::printf("Unable to %s process %d:\n", verb, o.pid);
        }
        if (err == kErrorInvalidParameter)
        {
            std::printf("Process does not exist.\n");
            return -1;
        }
        pstools::PrintSystemError(err);
        return -1;
    }

    inline void ReportRemoteSuccess(const Options& o, int count)
    {
        const char* const word = o.resume ? "resumed" : "suspended";
        if (count > 1)
        {
            std::printf("\r%d processes named %s %s on %s.\n\n", count, o.name.c_str(), word, o.computer.c_str());
        }
        else if (o.byName)
        {
            std::printf("\rProcess %s %s on %s.\n\n", o.name.c_str(), word, o.computer.c_str());
        }
        else
        {
            std::printf("\rProcess %d on %s %s.\n\n", o.pid, o.computer.c_str(), word);
        }
    }
} // namespace pssuspend
