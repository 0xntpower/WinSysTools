# PsKill (Sysinternals pskill64.exe v1.17)

Source-accurate C++20 console reconstruction. Terminates a process by PID or by
name, on the local machine or on a remote computer, with an optional whole-tree
kill. Reuses the shared PsTools command-line front end for the banner, the EULA
gate, alternate credentials, the error printer, the local computer name, and the
hidden password prompt.

- Original: `targets/sysinternals/pskill64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsKill/PsKill.cpp` plus `src/common/PsTools.h`, project
  `src/PsKill/PsKill.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The logic decompiled cleanly, so rea was not
  needed.

## What it does

```
Usage: pskill [-t] [\\computer [-u username [-p password]]] <process ID | name>
     -t    Kill the process and its descendants.
     -u    Specifies optional user name for login to remote computer.
     -p    Specifies optional password for user name.
     -nobanner Do not display the startup banner and copyright message.
```

A single target is required, given either as a numeric PID or as a process name.
A name matches case-insensitively as a prefix followed by a dot or the end of the
name, so `notepad` matches `notepad.exe`. With `-t` the whole descendant tree is
killed as well. With `\\computer` the kill runs on a remote host through the
PsKill helper service, and `-u` plus `-p` supply alternate credentials for it.
`-accepteula` and `-nobanner` are the standard Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x1400053E0 | OS gate, parse, prompt password, local name, run local or remote |
| `sub_140003490` | Banner plus EULA driver, run before parsing, declines exit 1 |
| `sub_1400029D0` | Banner printer, strips `-nobanner` from argv |
| `sub_1400020D0` | EULA gate, strips `-accepteula`, prompt or stored flag |
| `sub_140004B20` | PsKill command-line parse |
| `sub_140005220` | Usage |
| `sub_1400046A0` | Local kill driver, by name or by PID, optional tree walk |
| `sub_140004A10` | Kill one PID with OpenProcess plus TerminateProcess |
| `sub_1400045F0` | Process snapshot via NtQuerySystemInformation class 5 |
| `sub_140003180` | Enable a named privilege, used for SeDebugPrivilege |
| `sub_140004E00` | Remote kill through the PSKLLSVC named-pipe service |
| `sub_140003100` | Clear the current console line on stderr |
| `sub_1400035C0` | Print a Windows error code, netmsg.dll for codes 2100 to 2483 |
| `sub_140001800` | printf to stdout, used for all result messages and usage |

### Local kill

`sub_140004A10` opens the target with `PROCESS_TERMINATE | SYNCHRONIZE`
(`0x100001`), calls `WaitForSingleObject(h, 0)`, and only terminates when the
wait returns `WAIT_TIMEOUT`, which means the process is still running. A process
that has already signalled yields code 758 rather than a kill. The terminate exit
code is 1. A missing PID surfaces as `ERROR_INVALID_PARAMETER` (87) from
OpenProcess, which main turns into `Process does not exist.`

`sub_1400046A0` enables `SeDebugPrivilege`, then drives the by-name and tree
cases off `sub_1400045F0`, which is `NtQuerySystemInformation` with
`SystemProcessInformation` (class 5) into a buffer that grows until the query
fits. The reconstruction reads the same information class and walks the
`SYSTEM_PROCESS_INFORMATION` records through a struct whose named offsets land
exactly where the original indexes them (creation time at 0x20, image-name
UNICODE_STRING at 0x38, PID at 0x50, parent PID at 0x58). The tree walk climbs
each candidate's parent chain until it reaches the root, and it only considers a
candidate whose creation time is at or after the root's. That creation-time guard
is what keeps a recycled PID from being read as a descendant. The low bit of the
PID field doubles as a visited marker in the original, since real PIDs are
multiples of four. The reconstruction instead collects the kill set into a sorted
set, which gives the same single-kill-per-process result.

### Result messages

All result messages go to stdout. Local success:

- one process by name `Process %s killed.`
- one process by PID `Process %d killed.`
- many by name, no tree `%d processes named %s killed.`
- many by name with tree `%d processes named %s and their descendants killed.`
- many by PID with tree `%d processes descended from and including %d killed.`

Local failure prints `Unable to kill process %s:` or the `%d` form, then
`Process does not exist.` for code 87, otherwise the FormatMessage string on stderr.

### Remote kill

`sub_140004E00` deploys a bundled `PSKLLSVC.EXE` helper to the target's `ADMIN$`,
starts it through the SCM as service `PSKLLSVC`, then connects to its
`\\computer\pipe\pskllsvc` named pipe and exchanges a fixed 0x118-byte packet.
The packet is a leading `isName` byte, the name at offset 1, the PID at offset
0x108, the tree flag at offset 0x10C, and on the way back a status DWORD at
0x110 and a kill count at 0x114. On a nonzero status it prints
`Error killing process ... on %s:` and, for code 87, `Process %s does not exist
on %s.` Afterward it removes the service. The alternate credentials flow through
`WNetAddConnection2A` to `\\computer\IPC$`.

## Validation

Original and rebuild (x64 Release) run with the same arguments and `-nobanner`,
stdout and stderr compared, exit codes compared. Every live kill targeted a
throwaway process spawned for the run and addressed by the PID it reported, never
a shared name or a real process. The successful and tree cases used two fresh
dummies so each binary had its own live target, and the PID in the message was
normalized before the compare.

| Case | Target | stdout | stderr | exit |
|------|--------|--------|--------|------|
| banner plus usage, no args | n/a | identical | identical | -1 / -1 |
| bad argument `-x` | n/a | identical | identical | -1 / -1 |
| nonexistent PID `999999` | n/a | identical | identical | -1 / -1 |
| kill by PID | own hidden `ping` dummy | identical template | identical | 0 / 0 |
| kill by unique name | own renamed dummy | identical | identical | 0 / 0 |
| tree kill by PID | own `cmd` plus child `ping` | identical template | identical | 0 / 0 |

Sample outputs reproduced exactly:

```
Process 18888 killed.

Unable to kill process 999999:
Process does not exist.

3 processes descended from and including 160556 killed.
```

The tree case killed the same count as the original (the `cmd` root, its
`conhost`, and the child `ping`), which confirms the creation-time-guarded walk
matches. The remote path is reproduced but validated locally only.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsKill\PsKill.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsKill\PsKill.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Shared framework reuse

PsKill reuses `src/common/PsTools.h` (read-only) for `PrintBanner`, `StripFlag`,
`AcceptEula`, `PrintSystemError`, `LocalComputerName`, `PromptHiddenPassword`,
`RemoteConnect`, and `RemoteDisconnect`. It does not use the shared
`ParseCommandLine` or `ForEachComputer`, because PsKill accepts a single computer
and one PID-or-name target rather than the comma and response-file target lists
the rest of the suite take. The parse grammar, the `-t` switch, the PID-or-name
target, the process snapshot, the kill primitives, and the tree walk are all
implemented locally in `PsKill.cpp`.

## Deviations

1. **Remote helper service.** The original deploys a bundled `PSKLLSVC.EXE` to
   the remote `ADMIN$`, starts it through the SCM, talks to its named pipe, and
   removes it. That helper binary is out of scope, so the rebuild reproduces the
   faithful client half (the 0x118-byte request and response, the status and
   count fields, the credential connect, and the exact message and error paths)
   and leaves the service auto-deploy unreproduced. Validated locally only, as
   allowed.
2. **Single snapshot.** The original re-snapshots the process list in a loop to
   catch children spawned while a parent is being killed. The rebuild takes one
   snapshot and computes the full tree up front, which is correct for the ordinary
   case and skips the race rescan. The validated tree case gives the same count.
3. **Error-message flags.** The rebuild routes error codes through
   `pstools::PrintSystemError`, which omits `FORMAT_MESSAGE_IGNORE_INSERTS` that
   `sub_1400035C0` sets. The process error codes PsKill emits (5, 87, and the
   like) carry no insert tokens, so the output is the same.
4. **OS version gate.** The original's Windows XP or higher check is dropped as
   always true on supported systems.
5. **Banner source.** The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are grouped
separately. No commit was made.
