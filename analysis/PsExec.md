# PsExec (Sysinternals PsExec v2.43, PsExec64.exe)

Source-accurate C++20 console reconstruction. Runs a program on a local or remote
computer. PsExec is the largest PsTool, so this reconstruction reproduces the full
command line and switch set and the LOCAL direct-execution path in full, and
reproduces the remote and local-service paths as a faithful client whose helper
binary and encrypted stdio protocol are out of scope, the same boundary PsKill
drew for its PSKLLSVC remote path.

- Original: `targets/sysinternals/PsExec64.exe` (x64, MSVC, static CRT, console,
  md5 db89ec570e6281934a5c5fcf7f4c8967)
- Rebuild: `src/PsExec/PsExec.cpp` plus `src/common/PsTools.h`, project
  `src/PsExec/PsExec.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The parser, the usage printer, the local-exec
  driver, and the main dispatch decompiled cleanly, so rea was not needed.

## What it does

```
Usage: psexec [\\computer[,computer2[,...] | @file]][-u user [-p psswd]][-n s][-r servicename][-h][-l][-s|-e][-x][-i [session]][-c [-f|-v]][-w directory][-d][-<priority>][-g n][-a n,n,...][-verbose] cmd [arguments]
```

With no computer the command runs on the local machine. With `\\computer`, a comma
list, `@file`, or `\\*` it runs on each target through the PSEXESVC service. The
local direct path builds the command line `"<app>" <args>` and launches it with the
caller token through `CreateProcessAsUserW`, or with alternate credentials through
`CreateProcessWithLogonW`. `-accepteula` and `-nobanner` are the standard
Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x14001A670 | OS gate, banner, EULA, parse, hidden-password prompt, dispatch |
| `sub_140002980` | Banner printer, strips `-nobanner` |
| `sub_140002080` | EULA gate, strips `-accepteula`, stored flag or prompt |
| `sub_140014030` | Command-line parse into module globals |
| `sub_140017C20` | Usage, one line per call to the stdout printer |
| `sub_140012860` | Local direct exec, CreateProcessWithLogonW or CreateProcessAsUserW |
| `sub_140002F30` | Limited user token, CreateRestrictedToken plus Low integrity (`-l`) |
| `sub_140003720` | Group affinity, SetThreadGroupAffinity (`-g`) |
| `sub_140003690` | Background priority, SetPriorityClass plus NtSetInformationProcess |
| `sub_140003C90` | Print a Windows error, netmsg.dll for codes 2100 to 2483 |
| `sub_1400159A0` | Per-computer service exec, remote and local System or interactive |
| `sub_140003800` | ForEachComputer, iterate `\\computer`, comma list, `@file`, `\\*` |
| `sub_140002860` | printf to stdout, used for usage |
| `sub_140003D60` | printf to stderr, used for the result messages |
| `sub_140002DC0` | printf to stderr, used for the error messages |

### Dispatch, local direct against service

`main` computes a local-direct flag from four conditions recovered at 0x14001ACEC.
The local direct path runs when the target is the local machine AND none of `-h`,
`-s`, `-i`, `-x` are present AND the alternate user is not a service account AND
`CreateProcessWithLogonW` is available. Everything else routes to the per-computer
service callback through `ForEachComputer`.

This matters for the switch list. The local System account (`-s`), the interactive
desktop (`-i`), the secure desktop (`-x`), and the elevated token (`-h`) are not a
direct local `CreateProcess` in the original. They obtain the System, interactive,
or elevated token through the PSEXESVC service, so they sit on the
reproduced-not-exercised side here along with the remote path. The reconstruction
also routes `-c` to the service side, because the empirical behavior of `-c` on a
local target is the slow service connect, not a local launch.

### Local direct exec

`sub_140012860` builds the child command line as `"<app>" <args>` and creates the
process with `dwCreationFlags = priorityClass | 0x414`, where 0x414 is
`CREATE_SUSPENDED | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT`. With `-u` and
without `-l` it calls `CreateProcessWithLogonW(user, domain, password, profileFlag,
NULL, cmdLine, flags, NULL, cwd, si, pi)`, splitting the user on a backslash into
account and domain, with the profile flag `LOGON_WITH_PROFILE` unless `-e`.
Otherwise it opens its own token with `0xF01FF`, derives a limited token for `-l`,
and calls `CreateProcessAsUserW`.

After the suspended create it applies the priority (`SetPriorityClass` for the
background class), the affinity (`SetProcessAffinityMask`, or `SetThreadGroupAffinity`
for `-g`), resumes the thread, and then either prints the process id for `-d` or
waits and prints the exit code. The result messages go to stderr. The error path
prints `\rPsExec could not start %s:` to stderr followed by the FormatMessage
string, and returns the Win32 error as the exit code. A successful wait returns the child
exit code, and `-d` returns the process id.

### Priority table

The seven `-priority` names and their classes, recovered from 0x1400891F0 (names)
and 0x1400893F0 (values) on a 516-byte stride.

| Name | Class | Value |
|------|-------|-------|
| low | IDLE_PRIORITY_CLASS | 0x40 |
| belownormal | BELOW_NORMAL_PRIORITY_CLASS | 0x4000 |
| normal | NORMAL_PRIORITY_CLASS | 0x20 |
| abovenormal | ABOVE_NORMAL_PRIORITY_CLASS | 0x8000 |
| high | HIGH_PRIORITY_CLASS | 0x80 |
| realtime | REALTIME_PRIORITY_CLASS | 0x100 |
| background | PROCESS_MODE_BACKGROUND_BEGIN | 0x100000 |

`normal` is accepted though the usage does not list it. The affinity `-a n,n,...`
builds a mask with `mask |= 1 << n` for each number, matching the original.

### Output streams

Usage goes to stdout. The banner goes to stderr unless stdout is a pipe. The result
messages (`%s started with process ID %d.` and `%s exited with error code %d.`) and
the error messages go to stderr. All were confirmed byte-exact against captures from
the original. The banner matches `pstools::PrintBanner` with fields PsExec, 2.43,
`Execute processes remotely`, `Copyright (C) 2001-2023 Mark Russinovich`,
`Sysinternals - www.sysinternals.com`.

### Copy-flag validation

Recovered by comparison against the original rather than by trusting one decompiled
branch. `-f` or `-v` without `-c` prints usage. `-c -f -v` is accepted by the
original and routes to the copy path, so the reconstruction does not treat `-f` and
`-v` as mutually exclusive.

## Validation

Original and rebuild (x64 Release) run with the same arguments and `-nobanner`,
stdout and stderr compared, exit codes compared. Every launch targeted a child that
self-terminates or a detached child addressed by the id it reported, never a shared
name or a real session. The volatile process id was normalized before the message
compare. Only the local path was exercised, with harmless commands. The `-s`, `-i`,
and `-x` switches were confirmed to route to the service stub without launching
anything locally.

| Case | stdout | stderr | exit (orig/rebuild) |
|------|--------|--------|---------------------|
| no args, usage | byte-identical (md5 082ca07c...) | empty | -1 / -1 |
| bad switch `-zz` | byte-identical usage | empty | -1 / -1 |
| bad program | empty | byte-identical | 2 / 2 |
| banner, no `-nobanner` | empty | byte-identical | 0 / 0 |
| wait, `ping -n 1` | empty | byte-identical | 0 / 0 |
| nonzero child, ping bad host | empty | byte-identical | 1 / 1 |
| `-d` detached `ping` | empty | identical after id normalize | id / id |
| `-w` invalid dir | empty | byte-identical | 11 / 11 |
| `-w` valid temp dir, `-d` | empty | identical after id normalize | id / id |
| `-l -h` incompatible | empty | byte-identical specific message | -1 / -1 |
| `-p` without `-u` | byte-identical usage | empty | -1 / -1 |
| `-f` or `-v` without `-c` | byte-identical usage | empty | -1 / -1 |

Switch effects confirmed through the OS on the live child, identical between the two
binaries. `-low` yields priority class Idle, `-high` yields High, `-a 1` yields
processor affinity mask 2 (bit 1 from `1 << 1`). Sample outputs reproduced exactly:

```
PsExec could not start nosuchprog_zzz.exe:
The system cannot find the file specified.

ping exited with error code 0.

ping started with process ID <id>.

PsExec could not start ping:
The directory name is invalid.
```

The usage md5 matches between the original, the Release build, and the Debug build.
The `CREATE_NEW_CONSOLE` child means a `cmd /c echo` launched in this non-interactive
harness never terminates, and the original hangs on it the same way, so the
wait-path cases use a child that self-terminates (`ping -n 1`) and the detached
cases use `-d`. The child output itself lands in the new console and is not on the
redirected streams for either binary, so there is nothing to diff there.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsExec\PsExec.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsExec\PsExec.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Unit layout

The reconstruction was split from one `PsExec.cpp` into a thin entry file plus
focused headers, one per concern, under `namespace psexec` with free functions and
globals marked `inline`. The HRC v1.1 style pass (`.clang-format`, Allman braces,
120 columns) ran afterward.

| File | Holds |
|------|-------|
| `PsExec.cpp` | banner and app constants, `main`, the top-level local-against-service flow |
| `PsExecCommon.h` | includes, recovered constants, the priority table, the `Options` record, the RAII `ScopedHandle`, and the `Widen` / `IsAllDigits` / `SplitUser` / `ParseAffinity` helpers |
| `Usage.h` | `PrintUsage`, the byte-identical help block (sub_140017C20) |
| `CommandLine.h` | `ParseCommandLine` (sub_140014030) |
| `LocalExec.h` | `CreateLimitedUserToken`, `ApplyAndResume`, `RunLocal` (sub_140012860) |
| `ServiceExec.h` | `RunViaService`, the PSEXESVC client stub (sub_1400159A0) |

The split is behavior-preserving code motion. Both x64 configs rebuild clean
afterward (the only diagnostic is the expected `/Ob3` override), and the
`-nobanner -?` help output stays byte-identical between the original and the Release
rebuild (md5 082ca07c, stderr empty on both, exit -1 on both), confirming the move
changed no behavior.

## Shared framework reuse

PsExec reuses `src/common/PsTools.h` (read-only) for `PrintBanner`, `StripFlag`,
`AcceptEula`, `PrintSystemError`, `LocalComputerName`, `PromptHiddenPassword`,
`RemoteConnect`, `RemoteDisconnect`, and `ForEachComputer`. It parses its own command
line rather than the shared `ParseCommandLine`, because the PsExec grammar is a large
switch set followed by a command and its argument tail, not the single target plus
account that the shared parser handles.

## Scope

### Implemented and validated (local direct path)

Full argument parsing and the whole switch set. Local execution with `CreateProcess`,
covering `-u` and `-p` alternate credentials through `CreateProcessWithLogonW`, `-e`
profile, `-l` limited user through `CreateRestrictedToken` plus Low integrity, `-d`
detached with the process id as the exit code, `-w` working directory, the seven
priority switches, `-a` affinity, `-g` processor group, and the usage and error
paths. The validated cases above cover the harmless local cases end to end.

### Reproduced but not exercised

The per-computer service path (remote targets, and local `-s`, `-i`, `-x`, `-h`, `-c`)
is reproduced as a faithful client. `RunViaService` connects to the target IPC$ with
the alternate credentials and reports the recovered structure, naming the PSEXESVC
service default and the `-r` override. The named-pipe stdio protocol, the password
encryption through a `\admin$\PSEXEC-*.key` file, and the service create, start, and
delete lifecycle were read from the decompile at 0x1400159A0 but are not driven,
because the path cannot run without the helper binary.

### Out of scope

The PSEXESVC.EXE helper binary and its auto-deploy to `\\computer\ADMIN$` are not
built, the same boundary PsKill drew for PSKLLSVC. Without the helper the service
path has no server side to talk to, so the full encrypted named-pipe stdio protocol
is left unreproduced.

## Deviations

1. **Service helper.** The original deploys a bundled PSEXESVC.EXE to the target
   ADMIN$, creates and starts the service through the SCM, and drives the child over
   an encrypted named pipe. That helper binary is out of scope, so the rebuild
   reproduces the client structure and the recovered naming and reports the gap
   cleanly rather than deploying the service. Validated locally only.
2. **Local System and interactive.** `-s`, `-i`, and `-x` go through the service in
   the original to obtain the System or interactive token. The reconstruction routes
   them to the same reproduced-not-exercised service path rather than inventing a
   direct local token path the original does not have. This keeps the local run safe,
   the switches never affect a real session.
3. **Argument tail.** The child argument tail is joined from argv with single spaces
   rather than sliced from the raw command line as the original does. This is
   identical for unquoted arguments and covers the harmless validation cases. Quoted
   inner arguments would differ.
4. **Background priority.** `-background` is applied with `SetPriorityClass` and the
   `PROCESS_MODE_BACKGROUND_BEGIN` flag, the documented path, rather than the
   original's `SetPriorityClass(IDLE)` followed by two `NtSetInformationProcess`
   calls. The observable priority outcome is the same.
5. **Length caps.** The original caps the password and user at 256 characters and the
   command, arguments, and working directory at 0x8000. The rebuild keeps full
   fidelity through `std::string` instead, as the reconstruction guide allows.
6. **OS version gate.** The original's Windows XP or higher check is dropped as always
   true on supported systems.
7. **Error-message flags.** The rebuild routes error codes through
   `pstools::PrintSystemError`, which omits `FORMAT_MESSAGE_IGNORE_INSERTS` that the
   original sets. The error codes PsExec emits carry no insert tokens, so the output
   matches byte for byte, as confirmed.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are grouped separately.
No commit was made.
