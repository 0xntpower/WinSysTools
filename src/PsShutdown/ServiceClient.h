// PsShutdown service-client half: the countdown, message, and notification
// request the original exchanges with its helper over \pipe\psshutdownsvc, plus
// the progress, error, and success output (mirrors sub_140004D20).
#pragma once

#include "CommandLine.h"

namespace psshutdown
{
    // The 0x220-byte request and response the client exchanges with the PsShutdown
    // helper over its named pipe. Recovered from sub_140004D20: the flag word, the
    // allow-abort byte, the message, the countdown in seconds, the requesting
    // user, the reason, the notification seconds, and on the way back a status.
#pragma pack(push, 1)
    struct ShutdownPacket
    {
        DWORD flags;          // 0x000
        BYTE allowAbort;      // 0x004
        char message[263];    // 0x005
        DWORD countdown;      // 0x10C
        char user[260];       // 0x110
        DWORD reason;         // 0x214
        DWORD displaySeconds; // 0x218
        DWORD status;         // 0x21C
    };
#pragma pack(pop)
    static_assert(sizeof(ShutdownPacket) == 0x220, "psshutdownsvc packet must be 544 bytes");

    // Verb for the "Error %s %s:" line when the service returns a nonzero status.
    [[nodiscard]] inline const char* ErrorVerb(DWORD flags)
    {
        if (flags & kLock)
        {
            return "locking";
        }
        if (flags & kReboot)
        {
            return "rebooting";
        }
        if (flags & kHibernate)
        {
            return "hibernating";
        }
        if (flags & kSuspend)
        {
            return "suspending";
        }
        if (flags & kPoweroff)
        {
            return "powering off";
        }
        return "shutting down";
    }

    // Verb for the "scheduled to %s" success line.
    [[nodiscard]] inline const char* ScheduleVerb(DWORD flags)
    {
        if (flags & kReboot)
        {
            return "reboot";
        }
        if (flags & kSuspend)
        {
            return "suspend";
        }
        if (flags & kHibernate)
        {
            return "hibernate";
        }
        if (flags & kPoweroff)
        {
            return "power off";
        }
        if (flags & kMonitor)
        {
            return "turn monitor off";
        }
        return "shut down";
    }

    // Fill the request's requesting-user field with DOMAIN\user from the process
    // token, defaulting to "<Unknown>". Mirrors the token lookup in sub_140004D20.
    inline void FillRequestingUser(char* out, size_t outSize)
    {
        std::snprintf(out, outSize, "<Unknown>");
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return;
        }
        BYTE buffer[0x800] = {};
        DWORD returned = sizeof(buffer);
        char name[260] = {};
        char domain[260] = {};
        DWORD cchName = static_cast<DWORD>(std::size(name));
        DWORD cchDomain = static_cast<DWORD>(std::size(domain));
        SID_NAME_USE use = SidTypeUnknown;
        auto* info = reinterpret_cast<TOKEN_USER*>(buffer);
        if (::GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &returned) &&
            ::LookupAccountSidA(nullptr, info->User.Sid, name, &cchName, domain, &cchDomain, &use))
        {
            std::snprintf(out, outSize, "%s\\%s", domain, name);
        }
        ::CloseHandle(token);
    }

    // Reproduce the PsShutdown client half of the service protocol. Mirrors
    // sub_140004D20: connect to the target's \pipe\psshutdownsvc, send the 0x220
    // request, read the response, and print the same progress, error, and success
    // messages. Returns the worker result as the original does, zero on success.
    //
    // ponytail: the original first deploys the bundled PSSDNSVC.EXE to the target's
    // ADMIN$ and starts it through the SCM (sub_140003920), then tears it down
    // (sub_140003F70), and the service itself runs the countdown, the notification
    // dialog, and InitiateSystemShutdownEx. That helper executable and its service
    // lifecycle are out of scope, the same call PsSuspend makes for PSSPNDSVC. This
    // reproduces the faithful client protocol and every message and error path that
    // runs when the service is reached, or not reached, on the target. Because no
    // service binary is deployed, this client never schedules a real shutdown.
    [[nodiscard]] inline int ServiceClient(const Options& o, const std::string& target, const std::string& localName)
    {
        const bool isLocal = ::_stricmp(target.c_str(), localName.c_str()) == 0;
        const char* const display = isLocal ? "local system" : target.c_str();

        ClearLine();
        std::fprintf(stderr, "\rConnecting with psshutdown service on %s...", display);

        char pipe[MAX_PATH] = {};
        std::snprintf(pipe, std::size(pipe), "\\\\%s\\pipe\\psshutdownsvc", target.c_str());
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            ClearLine();
            std::fprintf(stderr, "\rError establishing communication with psshutdown service on %s:\n", display);
            pstools::PrintSystemError(::GetLastError());
            std::printf("\n");
            return 1;
        }

        ShutdownPacket packet = {};
        packet.flags = o.flags;
        packet.allowAbort = o.allowAbort ? 1 : 0;
        const char* const messageSource = o.haveMessage ? o.message.c_str() : "";
        std::strncpy(packet.message, messageSource, sizeof(packet.message) - 1);
        if (o.countdown >= 0)
        {
            packet.countdown = static_cast<DWORD>(o.countdown);
        }
        else
        {
            SYSTEMTIME now = {};
            ::GetLocalTime(&now);
            packet.countdown = static_cast<DWORD>(-o.countdown - 60 * (now.wMinute + 60 * now.wHour));
        }
        packet.reason = o.reason;
        packet.displaySeconds = static_cast<DWORD>(o.displaySeconds);
        FillRequestingUser(packet.user, sizeof(packet.user));

        DWORD moved = 0;
        if (!::WriteFile(h, &packet, sizeof(packet), &moved, nullptr))
        {
            ClearLine();
            std::fprintf(stderr, "\rError communicating with psshutdown service on %s:\n", display);
            pstools::PrintSystemError(::GetLastError());
            std::printf("\n");
            ::CloseHandle(h);
            return 1;
        }
        if (!::ReadFile(h, &packet, sizeof(packet), &moved, nullptr))
        {
            const DWORD err = ::GetLastError();
            ClearLine();
            std::fprintf(stderr,
                         "\rError communicating with psshutdown service on %s. The system may\n"
                         "not have been %s:\n",
                         display, (o.flags & kLock) ? "locked" : "shutdown");
            pstools::PrintSystemError(err);
            std::printf("\n");
            ::CloseHandle(h);
            return 1;
        }
        ::CloseHandle(h);

        if (packet.status != 0)
        {
            ClearLine();
            std::fprintf(stderr, "\rError %s %s:\n", ErrorVerb(o.flags), display);
            if ((o.flags & kLock) && packet.status == 1)
            {
                std::fprintf(stderr, "%s does not support the LockWorkStation API.\n\n",
                             isLocal ? "The local system" : target.c_str());
            }
            else if ((o.flags & (kHibernate | kSuspend)) && packet.status == 1)
            {
                std::fprintf(stderr, "%s does not support %s.\n\n", isLocal ? "The local system" : target.c_str(),
                             (o.flags & kSuspend) ? "suspend" : "hibernation");
            }
            else
            {
                pstools::PrintSystemError(packet.status);
                std::printf("\n");
            }
            return 1;
        }

        if (o.flags & kAbort)
        {
            std::printf("Shutdown of %s aborted.\n\n", display);
        }
        else if (o.flags & kLock)
        {
            std::printf("%s locked.\n\n", isLocal ? "The local system is" : target.c_str());
        }
        else if (o.flags & kLogoff)
        {
            std::printf("Console logoff initiated on %s.\n\n", display);
        }
        else
        {
            const DWORD seconds = packet.countdown % 60;
            const DWORD minutes = (packet.countdown % 3600) / 60;
            const DWORD hours = packet.countdown / 3600;
            std::printf("%s is scheduled to %s in %02d:%02d:%02d.\n\n", isLocal ? "Local system" : target.c_str(),
                        ScheduleVerb(o.flags), hours, minutes, seconds);
        }
        return 0;
    }
} // namespace psshutdown
