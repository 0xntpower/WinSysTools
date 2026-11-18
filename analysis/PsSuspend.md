# PsSuspend (Sysinternals pssuspend64.exe v1.08)

Source-accurate C++20 console reconstruction. Suspends or resumes a process by
PID or name, on the local machine or a remote one, with the shared PsTools
command-line front end. Reuses `src/common/PsTools.h` read only.

- Original: `targets/sysinternals/pssuspend64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsSuspend/PsSuspend.cpp` plus `src/common/PsTools.h`, project
  `src/PsSuspend/PsSuspend.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The logic decompiled cleanly, so rea was not
  needed.

## What it does

```
Usage: pssuspend [-r] [\\RemoteComputer [-u Username [-p Password]]] <process Id or name>
```

Default action is suspend. `-r` resumes instead. The process can be named by its
numeric PID or by image name. A name acts on every matching process. With
`\\computer` the work runs on a remote machine through the PsSuspend helper
service, with `-u` and `-p` supplying alternate credentials. `-accepteula` and
`-nobanner` are the standard Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x140005B30 | OS gate, parse, choose local or remote, report the result |
| `sub_140003490` | Front end driver, run first: version info, banner, EULA |
| `sub_1400029D0` | Strip `-nobanner`, select stdout or stderr, print the banner |
| `sub_140002C80` | Short banner from the five version-resource fields |
| `sub_140001B80` / `sub_1400020D0` / `sub_1400022F0` | EULA satisfied check, strip `-accepteula`, prompt or dialog, write the flag |
| `sub_140004FD0` | Parse PsSuspend's own grammar (`-r`, `\\computer`, `-u`, `-p`, PID or name) |
| `sub_140005960` | Usage |
| `sub_1400045F0` | Local worker: enable SeDebugPrivilege, resolve ntdll, enumerate, act |
| `sub_1400048D0` | Act on one process: OpenProcess 0x800 then NtSuspendProcess or NtResumeProcess |
| `sub_1400052B0` | Remote worker over the PSSPNDSVC service and its named pipe |
| `sub_1400036F0` | Remote banner, EULA, credentialed connect, service deploy and start |
| `sub_140003D40` | Remote service stop and delete |
| `sub_140003180` | Enable one privilege (OpenProcessToken, AdjustTokenPrivileges) |
| `sub_1400035C0` | Print a Windows error, netmsg.dll for codes 2100 to 2483 |
| `sub_140001800` | printf to stdout, the wrapper every message goes through |

### Front end and the shared PsTools framework

The banner and EULA run before the OS check and the parse, driven from
`sub_140003490`. `sub_1400029D0` removes `-nobanner` from argv, then prints the
five-field banner in the format `\n%s v%s - %s\n%s\n%s\n\n` to stdout when stdout
is a pipe and to stderr otherwise. `sub_1400020D0` removes `-accepteula`, checks
the stored flag or prompts, and writes
`HKCU\Software\Sysinternals\PsSuspend\EulaAccepted`. A decline writes
`Eula declined.` to stderr and exits 1. This is the same front end recovered from
PsGetsid, so `src/common/PsTools.h` supplies all of it (`StripFlag`,
`PrintBanner`, `AcceptEula`, `PrintSystemError`, `LocalComputerName`,
`PromptHiddenPassword`, `RemoteConnect`, `RemoteDisconnect`).

### Argument grammar

PsSuspend has its own grammar, narrower than `pstools::ParseCommandLine`, so the
parser here mirrors `sub_140004FD0` directly rather than calling the shared one.
A leading `-` or `/` marks a switch: `r` resumes, `u` takes the next argument as
the user, `p` takes the next as the password. A `\\` token names the computer. An
all-digit token is a PID. Anything else is a process name. Validation requires a
process, requires a computer when `-u` is present, and requires `-u` when `-p` is
present. A failure prints the usage and exits with -1. The default suspend flag
byte is 1 in the image data, confirmed by reading `byte_140043190`.

### Local suspend and resume

`sub_1400045F0` enables `SeDebugPrivilege`, then resolves `NtSuspendProcess`,
`NtResumeProcess`, `NtSuspendThread`, `NtResumeThread`, and
`NtQuerySystemInformation` from ntdll by `GetModuleHandleA` plus
`GetProcAddress`. For a name it walks the `NtQuerySystemInformation` process list
and matches the image name against the target up to a `.` or the end, so
`notepad` matches `notepad.exe`. For a PID it finds the matching entry. Each hit
goes to `sub_1400048D0`, which opens the process with `PROCESS_SUSPEND_RESUME`
(0x800) and calls `NtSuspendProcess` or `NtResumeProcess` on the handle. The
count of affected processes is returned, and the final line reads
`Process X suspended.`, `Process X resumed.`, or
`N processes named X suspended.` for a multi-match. A failure prints
`Unable to suspend process X:` and then `Process does not exist.` for error 87 or
the formatted system error otherwise.

When `NtSuspendProcess` is absent the original falls back to a thread-by-thread
walk with `NtOpenThread` plus `NtSuspendThread`. That branch exists for very old
NT releases that predate `NtSuspendProcess` and never runs on Windows XP or
later, where the call is always present.

### Remote suspend and resume

`sub_1400052B0` deploys the bundled `PSSPNDSVC.EXE` to the remote `ADMIN$`, starts
it through the service control manager, then opens `\\computer\pipe\psspndsvc`.
It sends a 276-byte request and reads a 276-byte response:

| Offset | Field |
|--------|-------|
| 0x000 | suspend flag |
| 0x001 | by-name flag |
| 0x002 | process name, up to 260 bytes |
| 0x108 | PID |
| 0x10C | status returned by the service |
| 0x110 | affected-process count returned |

It prints `Connecting with PsSuspend service on %s...`, then
`Suspending process %d on %s...`, and on success the main summary line
`Process %s %s on %s.` A status of 87 from the service yields
`Process %s does not exist on %s.`

## Validation

Original and rebuild (x64 Release) run with the same arguments and `-nobanner`,
each acting on its own throwaway dummy process that the harness spawned and owns.
stdout, stderr, and exit code were compared, with the PID or the unique dummy
name normalized out. No real system, service, or user process was touched.

A heartbeat dummy proved the state change directly. It appended a line every
150 ms. The rebuild suspended it by PID, the line count stopped growing, the
rebuild resumed it, and the count grew again. The dummy was then killed.

| Case | stdout | stderr | exit |
|------|--------|--------|------|
| suspend by PID | identical | identical | 0 / 0 |
| resume by PID (`-r`) | identical | identical | 0 / 0 |
| suspend by name | identical | identical | 0 / 0 |
| resume by name (`-r`) | identical | identical | 0 / 0 |
| multi-match by name (4 of a kind) | identical | identical | 0 / 0 |
| nonexistent PID | identical | identical | -1 / -1 |
| bad argument (`-z`) | identical | identical | -1 / -1 |
| no arguments | identical | identical | -1 / -1 |
| banner, no `-nobanner` | identical | identical | -1 / -1 |

Sample outputs reproduced exactly:

```
Process 296492 suspended.

4 processes named psdum2 suspended.

Unable to suspend process 999999:
Process does not exist.
```

The remote client path was exercised locally against a host with no service. The
rebuild printed the `Connecting with PsSuspend service on NOSUCHHOST123...` line,
then the service-communication error to stdout and the formatted network error to
stderr, and exited -1. The wording and the streams match the client half of
`sub_1400052B0`.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsSuspend\PsSuspend.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsSuspend\PsSuspend.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Local additions

`src/common/PsTools.h` is read only and does not carry these, so they live in
`PsSuspend.cpp`: the PsSuspend argument parser, `EnablePrivilege` for
`SeDebugPrivilege` (mirrors `sub_140003180`), `ResolveNtProc` for the ntdll
lookups, `ClearLine` (mirrors `sub_140003100`), and the `SuspendPacket` layout.

## Deviations

1. **Remote service auto-deploy**. The original copies `PSSPNDSVC.EXE` to the
   remote `ADMIN$` and runs it through the service control manager before talking
   to its pipe, and removes it afterward. That helper binary is out of scope, the
   same call made for PsGetsid's GETSIDSV helper. The rebuild reproduces the
   faithful client half, the 276-byte request and response and every message and
   error path, and leaves the service lifecycle unreproduced. Remote validation
   was local only, as allowed.
2. **Legacy thread-walk fallback**. The original suspends thread by thread when
   `NtSuspendProcess` is absent. That branch is dead on Windows XP and later,
   where the call always exists, so the rebuild carries only the
   `NtSuspendProcess` and `NtResumeProcess` path.
3. **OS version gate**. The original's "Windows XP or higher" check is dropped as
   always true on supported systems.
4. **Name enumeration source**. The rebuild enumerates with
   `CreateToolhelp32Snapshot` rather than
   `NtQuerySystemInformation(SystemProcessInformation)`. The match rule, the
   per-process action, and the count are the same.
5. **PID lookup**. The rebuild opens the PID directly rather than first locating
   it in the process list. A nonexistent PID yields error 87 and the same
   `Process does not exist.` line either way.
6. **Banner source**. The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.
7. **Numeric parse**. `std::from_chars` replaces the original `atoi`. Both agree
   on a valid numeric PID.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are tracked
separately. No commit was made.
