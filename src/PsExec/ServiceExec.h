// PsExec per-computer service path (mirrors the client of sub_1400159A0): the
// remote target and the local -s / -i / -x / -h / -c runs that obtain the System,
// interactive, or elevated token through PSEXESVC. The helper-binary deploy to
// ADMIN$ and the encrypted named-pipe stdio are out of scope, the same boundary
// PsKill drew for its PSKLLSVC remote path, so this reports the gap and does not
// run.
#pragma once

#include "PsExecCommon.h"

namespace psexec
{
    // Run the command on one computer through the PSEXESVC service. Connects to the
    // target's IPC$ with the alternate credentials, reports the recovered client
    // structure, and returns ERROR_NOT_SUPPORTED without driving the service.
    [[nodiscard]] inline int RunViaService(const char* computer, const Options& o)
    {
        bool connected = false;
        if (o.haveUser)
        {
            connected = pstools::RemoteConnect(computer, o.user.c_str(), o.password.c_str());
        }

        std::fprintf(stderr,
                     "\rPsExec service execution on %s is reproduced as a faithful client only.\n"
                     "The %s service deploy to ADMIN$ and the named-pipe stdio protocol are out of\n"
                     "scope, so this path is not exercised. Run a local command to use PsExec here.\n",
                     computer, o.serviceName.c_str());

        if (connected)
        {
            pstools::RemoteDisconnect(computer);
        }
        return static_cast<int>(ERROR_NOT_SUPPORTED);
    }
} // namespace psexec
