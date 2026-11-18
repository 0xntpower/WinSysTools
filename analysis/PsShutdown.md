# PsShutdown

Source-accurate C++20 console reconstruction of Sysinternals **psshutdown64.exe v2.6**.

## SAFETY WARNING — read before touching this tool

PsShutdown really shuts the machine down. During the first attempt to recreate it, a validation run of the shutdown tool powered this machine off and killed the working session. **Never run the original `targets/sysinternals/psshutdown64.exe` with any argument, and never run the rebuild `src/PsShutdown` with anything other than `/?`.**

- The **original** bundles a working helper service and actually shuts down on almost any action invocation.
- The **rebuild**'s local path reaches `ImmediateShutdown` (a direct `ExitWindowsEx` or `SetSuspendState`) when the target is local, an action is set, and the countdown is 0. That call is not abortable by `shutdown /a`. The shared watchdog only catches a scheduled shutdown with a countdown, not an immediate one.
- Safe rebuild invocations, by code trace: `/?` (Parse returns false before `Worker`), `-a` (the abort bit skips the immediate branch and reaches the no-service connect error), and an action with a positive `-t` (a nonzero countdown skips the immediate branch and reaches the no-service connect error). Everything else is unsafe. Prefer `/?` only.

## RE findings (IDA idalib, psshutdown64.exe v2.6)

- `main` `sub_140005EF0` runs the OS gate, the shared front end, the parser, and the per-target loop.
- Front end `sub_1400036C0` reads the version resource for the banner and runs the EULA gate (`HKCU\Software\Sysinternals\PsShutdown\EulaAccepted`).
- Usage `sub_140005B30` prints the switch help, then appends the live reason table from User32 `BuildReasonArray`.
- Parser `sub_140005500` has its own grammar (nine mutually exclusive action bits, force and allow-abort modifiers, `-t`, `-e`, `-m`, `-u`, `-p`, `-n`, `-v`, targets). No action named defaults to poweroff. More than one action prints usage.
- Per-target worker `sub_140004A90`: a local target with no countdown and no abort runs the immediate action directly through `sub_140004840` (`ExitWindowsEx`, Powrprof `SetSuspendState`) or `sub_140004820` (monitor off). Anything else goes through the service client.
- Service client `sub_140004D20` exchanges a 0x220-byte request over `\pipe\psshutdownsvc`. The 544-byte packet layout (flags, allow-abort, message, countdown, requesting user, reason, notify seconds, status) is recovered and reproduced.
- The service deploy and teardown (`sub_140003920` / `sub_140003F70`) and the bundled PSSDNSVC.EXE helper that runs the countdown, the notification dialog, and `InitiateSystemShutdownEx` are out of scope, the same way PsSuspend leaves PSSPNDSVC. The recreation reproduces the faithful client half and every message and error path.
- Shared helpers factored alongside: target iteration `sub_140003260` (single, comma list, @file, * domain), enable a privilege `sub_140003180` (OpenProcessToken, AdjustTokenPrivileges), clear console line `sub_140003100` (padding written to stderr), print a system error `sub_1400037F0` (FormatMessage, netmsg.dll for net codes).

## Build

VS 2022 Community MSBuild, v143, stdcpp20, Unicode, static CRT, SubSystem Console. Debug and Release x64 both build clean (only the shared `/Ob2` to `/Ob3` note from `Directory.Build.targets`). Reuses `src/common/PsTools.h` read-only for the banner, EULA, local identity, hidden password prompt, error text, and target iteration. The parser and the action and service paths are local to `PsShutdown.cpp`.

## Validation (usage-only, by design, zero shutdown risk)

The action and service paths are validated by code inspection against the decompiled `sub_*` map above, not by execution, because running either binary with an action can power the machine off. Usage was validated without executing the original at all:

- The original's strings were extracted statically with our own Strings recreation run against `psshutdown64.exe` (Strings reads the file and does not execute it).
- The rebuild's `/?` output (the only psshutdown invocation run, Parse returns false before `Worker`) matches the original's usage description lines byte-for-byte (every checked line present in the original's strings).
- Machine uptime was unchanged across the whole validation, confirming no shutdown happened.

## Refactor into units plus HRC style pass (2026-11-18)

`PsShutdown.cpp` was split from one large file into a thin entry file plus five headers, each under `namespace psshutdown` with free functions and globals `inline` and classes kept:

- `ShutdownCommon.h` the action and modifier bit constants, the reason masks, `ClearLine`, `EnablePrivilege`, `Upper`.
- `CommandLine.h` the `Options` struct and the switch grammar (`ParseReason`, `ParseClockCountdown`, `Parse`).
- `Usage.h` the usage block and the live reason table (`PrintUsage`).
- `ServiceClient.h` the `ShutdownPacket`, the verb helpers, `FillRequestingUser`, and `ServiceClient`.
- `ShutdownActions.h` the immediate local actions (`ImmediateShutdown`, `MonitorOff`, `LocalLock`) and the per-target `Worker`.

The banner constants and `main` stay in `PsShutdown.cpp`. The move was byte-for-byte with no logic change. An order-independent comparison of the pre-refactor file against the combined new sources matched all 475 statement units, the only difference being a clang-format split of one banner string literal into two adjacent literals that the compiler rejoins. The immediate-shutdown guard is unchanged. Headers were wired into the project's ClInclude and `clang-format` was run over the whole tool.

Revalidated compile-only plus `/?` (zero shutdown risk, same discipline as the original validation):

- Debug and Release x64 both build clean (only the shared `/Ob2` to `/Ob3` note from `Directory.Build.targets`).
- A single `/?` run of the rebuilt Release binary was the only invocation executed. It printed the banner, the full usage block, and the live reason table, matching the documented usage above, and returned -1 (exit 255) before reaching `Worker`. No shutdown happened.
- The original `psshutdown64.exe` was not executed at any point, with any argument.

## Deviations

- The helper service binary and its countdown, notification dialog, and `InitiateSystemShutdownEx` call are reproduced by inspection only, the same boundary PsSuspend and PsExec take for their helper services.
- Live action execution (shutdown, reboot, logoff, lock, hibernate, suspend, monitor off, remote) was deliberately not performed on either binary for machine safety.
- The OS version gate is dropped, matching the PsSuspend precedent, because it is always true on supported systems.
