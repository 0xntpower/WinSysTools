# PsService (Sysinternals PsService64.exe v2.26)

Source-accurate C++20 console reconstruction. A service viewer and controller
over the Service Control Manager, with the shared PsTools startup banner and
EULA front end. Lists and queries services, reads their configuration,
dependencies, and security, searches the network for a service, and (faithfully
in code, not exercised here) starts, stops, pauses, continues, restarts, and
reconfigures them.

- Original: `targets/sysinternals/PsService64.exe` (x64, MSVC, static CRT, console, Unicode)
- Rebuild: `src/PsService/PsService.cpp` plus `src/common/PsTools.h`, project
  `src/PsService/PsService.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The whole dispatch and every printer decompiled
  cleanly, so rea was not needed.

## What it does

```
Usage: PsService64.exe [\\Computer [-u Username [-p Password]]] <cmd> <optns>
```

Commands: `query`, `config`, `setconfig`, `start`, `stop`, `restart`, `pause`,
`cont`, `depend`, `find`, `security`. Omitting a command runs a `query` over all
Win32 services on the target. `-u` and `-p` supply alternate credentials for a
remote computer. `-accepteula` and `-nobanner` are the standard Sysinternals
switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x1400070E0 | setlocale, banner plus EULA, parse, prompt password, open SCM local or remote, dispatch |
| `sub_140004E10` | command-line parser, fills the command code and the command-argument index |
| `sub_140006D00` | usage printer, one block per command code 0 to 12 |
| `sub_140005600` | command dispatcher, switches on the command code |
| `sub_140005490` | open the SCM with `OpenSCManagerW(machine, "ServicesActive", MAXIMUM_ALLOWED \| SC_MANAGER_CONNECT)` |
| `sub_140005170` | enumerate with `EnumServicesStatusExW` (SC_ENUM_PROCESS_INFO) |
| `sub_1400054D0` | open one service by name, falling back to a display-name match through an enumeration |
| `sub_140004000` | status printer for `query`, `depend`, `start`, and the control commands |
| `sub_140003BA0` | config printer for `config` |
| `sub_140004450` | security printer for `security` |
| `sub_140006080` | `query` handler (option parse plus per-service status) |
| `sub_140006480` | `config` handler |
| `sub_1400066C0` | `security` handler |
| `sub_1400059D0` | `depend` handler (`EnumDependentServicesW`) |
| `sub_140005C90` | `find` handler (`NetServerEnum` plus per-server query) |
| `sub_140006B10` | `start` handler (`StartServiceW`) |
| `sub_1400057A0` | `stop`, `pause`, `cont`, and the restart stop phase (`ControlService`) |
| `sub_1400068D0` | `setconfig` handler (`ChangeServiceConfigW`) |
| `sub_1400052C0` | manual SID to string, same output as `ConvertSidToStringSid` |
| `sub_140002F30` / `sub_140003310` | clear-line progress eraser used by `find`, writes to stderr |
| `sub_140003240` | print a Windows error code, netmsg.dll for codes 2100 to 2483 |
| `sub_140001770` family | the formatted console writer `sub_1400017D0`, which is `wprintf` to stdout |

### Command codes

The parser maps keywords to a code read by the dispatcher and the usage printer:
`query`/`queryeex`/`interrogate` to 1 or 2, `stop` to 3, `start` to 4, `pause`
to 5, `cont`/`continue` to 6, `config` to 7, `depend`/`enumdepend` to 8, `find`
to 9, `restart` to 10, `setconfig` to 11, `sec`/`security` to 12. Codes 1 and 2
run the same `query` body.

### Read commands

- **query / list.** `EnumServicesStatusExW` over SERVICE_WIN32 (type 0x30) and
  SERVICE_STATE_ALL by default. `-t` chooses driver (0x0B), service (0x30),
  interactive, or all (0x3B). `-s` chooses active, inactive, or all. `-g` filters
  by load-order group. A named argument is matched as a case-insensitive prefix
  of either the service name or the display name, and switches the default type
  to all (0x3B). Each match is opened with SERVICE_QUERY_CONFIG plus
  SERVICE_QUERY_STATUS plus SERVICE_INTERROGATE (0x85) and printed by the status
  routine, which reads `QueryServiceConfigW`, the description from
  `QueryServiceConfig2W` level 1 with a single trailing CR or LF stripped, and
  `QueryServiceStatus`. The format carries the exact tabs and column widths of
  SERVICE_NAME, DISPLAY_NAME, the description line, GROUP, TYPE, STATE, the
  controls-accepted flags, WIN32_EXIT_CODE, SERVICE_EXIT_CODE, CHECKPOINT, and
  WAIT_HINT.
- **config.** Same enumeration and prefix match, service opened with
  SERVICE_QUERY_CONFIG plus SERVICE_QUERY_STATUS (0x05). The config routine adds
  START_TYPE with the delayed-autostart flag from `QueryServiceConfig2W` level 3,
  ERROR_CONTROL, BINARY_PATH_NAME, LOAD_ORDER_GROUP, TAG, the DEPENDENCIES
  multi-string with one entry per line, SERVICE_START_NAME, and the failure
  actions from level 2. The description here is printed without the CR or LF
  stripping the status routine applies.
- **depend.** Opens the named service with SERVICE_ENUMERATE_DEPENDENTS (0x08),
  calls `EnumDependentServicesW` with SERVICE_STATE_ALL, prints
  `%s has no dependent services.` on an empty set, otherwise opens each dependent
  with 0x85 and runs the status routine.
- **find.** `NetServerEnum` level 100 with SV_TYPE_NT. For each server it builds
  the UNC name, opens that server's SCM, opens the service, and reports the
  server when the service runs, or when `all` was given, for any state. Progress
  is a carriage-return line cleared on stderr.
- **security.** Same enumeration, exact (not prefix) name match, service opened
  with READ_CONTROL plus SERVICE_QUERY_CONFIG (0x20001). The security routine
  reads the owner and DACL through `QueryServiceObjectSecurity`, resolves each
  ACE SID with `LookupAccountSidW` (SID string on failure), and lists each access
  bit by name after `MapGenericMask` against the standard service generic
  mapping. An absent DACL prints the `Builtin\Everyone: ALL` line.

### State-changing commands

Reproduced faithfully in code and not run during validation. `start` opens with
0x95 and calls `StartServiceW` with the trailing arguments. `stop`, `pause`, and
`cont` open with the matching extra access bit added to 0x85 and call
`ControlService`, then print the new status, with stop treating
ERROR_SERVICE_NOT_ACTIVE as success. `restart` runs the stop control, polls
`QueryServiceStatus` every 500 ms until STOPPED, then runs `start`. `setconfig`
opens with SERVICE_CHANGE_CONFIG (0x02) and calls `ChangeServiceConfigW` with the
new start type (auto, demand, or disabled), leaving every other field unchanged.

### Shared PsTools front end

Reused read-only from `src/common/PsTools.h`: the startup banner (fixed version
fields for v2.26), the EULA gate under `HKCU\Software\Sysinternals\PsService`,
and `PrintSystemError`, which matches `sub_140003240` (`FormatMessage` to stderr,
netmsg.dll for the LANMAN range 2100 to 2483). PsService is a fully Unicode tool
while that header is ANSI-first, so the wide pieces the header does not cover are
implemented locally and marked in the source: the argument parser, the wide local
computer name, the wide IPC$ credential connect and disconnect, and the hidden
password prompt.

## Validation

Original and rebuild (x64 Release) run with the same arguments and `-nobanner`,
stdout compared byte for byte. All read-only. No service was started, stopped,
paused, continued, or reconfigured. The commands are deterministic back to back.

| Command | stdout |
|---------|--------|
| `query Dnscache` | identical |
| `query Schedule` | identical |
| `config Dnscache` | identical |
| `config Schedule` | identical except one failure-action label (see below) |
| `depend RpcSs` | identical |
| `depend Dnscache` | identical |
| (no command, query over all services) | identical, 3724 lines |
| `find Dnscache` | identical (both report the browse error and the not-found line) |

### The one difference

`config Schedule` differs on a single line, the first FAILURE_ACTIONS entry.
Schedule has three recovery actions whose first `SC_ACTION` carries `Type == 4`,
a value outside the documented SC_ACTION range of 0 to 3. The original indexes a
four-entry label table with that out-of-range type and reads past its end into
adjacent globals, so it prints the bytes of the service generic mapping followed
by the machine name string as a wide label. That output depends on the original
binary's exact global layout and cannot be reproduced from a separate image. The
rebuild prints an empty label for the unknown type, which is the faithful
behaviour minus the out-of-bounds read. The other two actions on Schedule
(`Restart` at 60000 and `None` at 0) match exactly, as does every other line of
every other service.

Note on the DELAY unit. Both the original and the rebuild print the raw
`SC_ACTION.Delay` field under a `seconds` label although the field holds
milliseconds. The rebuild keeps this quirk so the numbers match the original.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags and the
static CRT. The only diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3`
from those flags.

```
MSBuild src\PsService\PsService.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsService\PsService.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Deviations

1. **Failure-action label for an out-of-range action type.** Described above. The
   original reads past its label table and prints layout-dependent garbage. The
   rebuild prints an empty label, keeping full data fidelity without the
   out-of-bounds read.
2. **QueryServiceConfig2W binding.** The original binds it from advapi32 at run
   time and guards every call on a non-null pointer. The rebuild does the same.
   On every supported system the pointer is non-null, so the description,
   delayed-autostart, and failure-action lines always appear.
3. **Remote credential connect and the hidden password prompt.** PsService is
   Unicode throughout, so the wide IPC$ connect and disconnect and the hidden
   password prompt are implemented locally rather than through the ANSI helpers
   in the shared header. The remote path was not exercised in validation, as
   allowed.
4. **OS version gate.** The original starts with a version check whose failure
   message reads `PsLogList requires Windows XP or higher.` (a copy artifact in
   the original). It is always satisfied on supported systems and is dropped.
5. **Banner source.** The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are consolidated
separately. No commit was made. The shared `src/common/PsTools.h` was reused
read-only and not edited.
