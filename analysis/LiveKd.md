# LiveKd (Sysinternals) - reconstruction notes

Console recreation of the Sysinternals **LiveKd** utility, version 5.65. This is a
command-line tool, not a WTL GUI, and it is not a PsTool, so the NirSoft toolbar
and window conventions and the shared `PsTools.h` front end do not apply. Source
lives at `src/LiveKd/` (`LiveKd.cpp` plus `LiveKd.vcxproj`), built as an x64
console Application with the static CRT.

Original binary: `targets/sysinternals/livekd64.exe`
(MD5 `a4a2ac20345b3d1fb8a59628708fce70`, SHA-256
`e2884fa2baa44a092fc7e8a6fe59fc02af754ff18b1e10ecaebd91a7097d5fb9`, 394080 bytes).
Version resource InternalName `livekd`, FileVersion `5.65`, company
`Sysinternals - www.sysinternals.com`, downloaded fresh from the Sysinternals
distribution point and the 64-bit image extracted.

## What it does

LiveKd runs the kernel debugger (`kd` or `windbg` from the Debugging Tools for
Windows) against the live system, or writes a live kernel dump to disk. It prepares
a source of kernel memory, then either launches the configured debugger pointed at
that source, or with `-o` writes a dump file. The historical mechanism was the
local-kernel-debugging driver `LiveKdD.SYS`. Modern Windows adds the native live
dump API `NtSystemDebugControl(SysDbgGetLiveKernelDump)`, which snapshots kernel
memory without bugchecking the machine.

Command line (from the recovered usage block):

```
livekd [-w|-k debugger-path|-o dumpfile] [-m[flags]] [-mp process|pid] [-vsym] [-b] [debugger options]
livekd [-w|-k debugger-path|-o dumpfile] -ml [-hvd] [-b] [debugger options]
livekd -hvl
livekd [-w|-k debugger-path|-o dumpfile] -hv guid|name [-p] [-vsym] [-b] [debugger options]
livekd [-w|-k debugger-path] -hv guid|name -hvkl [-vsym] [-b] [debugger options]
```

## Manifest level (RE finding)

The embedded manifest requests **`asInvoker`**, not `requireAdministrator`. LiveKd
does not self-elevate. Instead it checks token elevation at runtime (sub_140008170)
and prints `Livekd requires administrative rights. You must run it from an elevated
command prompt.` when not elevated. The recreation matches this: the default
console-subsystem manifest (`asInvoker`) is used, and the elevation check is done in
code. This corrects the task's working assumption of `requireAdministrator`.

## Scope and safety

The recreation reproduces the **modern, safe live-dump path only**. The `-o`
dumpfile creation and the live view handed to the debugger both go through
`NtSystemDebugControl` with `SysDbgGetLiveKernelDump` (command 37), which produces a
consistent kernel memory snapshot without crashing the system. Nothing in the
recreation can bugcheck, reboot, log off or power off the machine.

Reproduced faithfully:

- Startup order (banner, EULA gate, elevation check, SeDebugPrivilege).
- The banner (line one byte-identical to the original).
- The EULA flag `HKCU\Software\Sysinternals\LiveKd\EulaAccepted` and the suite-wide
  flags under HKLM and HKCU `Software\Sysinternals`, plus `-accepteula`.
- Argument parsing for the documented switches (`-w`, `-k`, `-o`, `-ml`, `-hvd`,
  `-md`, `-mp`, `-vsym`, `-b`, `-y`, `-?`) and the pass-through of every other token
  to the debugger command line.
- The usage text (byte-identical, 39 lines).
- Debugger discovery (sub_14000B7D0 and sub_14000B570): the `-k` override, `-w`
  windbg versus the default kd, the candidate directories
  `%ProgramFiles%\Debugging Tools for Windows (x64)`,
  `%ProgramFiles%\Debugging Tools for Windows 64-bit` and `C:\debuggers`, the
  registry root `HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots` value
  `WindowsDebuggersRoot` with the `\x64` suffix, and `SearchPath` for the image.
- The `_NT_SYMBOL_PATH` handling (the symbol-configuration half of sub_14000AAD0):
  the dbghelp presence check, and, when symbols are not configured, the prompt to
  point `_NT_SYMBOL_PATH` at the Microsoft symbol server
  `srv*<folder>*http://msdl.microsoft.com/download/symbols` with `c:\Symbols` as the
  default folder, written to the environment and to `HKCU\Environment`.
- The native live kernel dump writer (sub_140009830) with the exact
  `SYSDBG_LIVEDUMP_CONTROL` layout and flags, and the restrictive dump-file DACL.
- The debugger launch and the re-run loop (`Execute kd again? (y/n)`), with `-z` to
  open the dump (and `-e` for `Dumpchk`), and batch mode (`-b`).

Out of scope, documented and deliberately not reproduced because they depend on the
legacy driver, extra services, or the unsafe path:

- The `LiveKdD.SYS` local-kernel-debugging driver path in sub_140009BE0 (the
  `DeviceIoControl` codes `0x23450000`, `0x23450004`, `0x2345000C`). This is the
  original's driver-backed live view and the `-m` mirror dump. The recreation uses
  the native snapshot API in its place.
- Hyper-V VM debugging (`-hv`, `-hvl`, `-hvkl`, `-hvkb`, `-p`), which needs the
  Hyper-V WMI provider and COM.
- `-dumpconvert` and the full ntoskrnl symbol-offset resolution that the driver
  path needs (MmPfnDatabase, MmDbgCopyMemory, the KPRCB and KPROCESSOR_STATE
  offsets).

## Reverse engineering (IDA idalib MCP primary)

Key functions recovered from the binary:

| Address | Role |
|---|---|
| `0x14000c020` | `main` - startup, argument parsing, dispatch |
| `0x14000bfe0` | banner printer |
| `0x14000bef0` | own version-resource reader (the `%lu.%lu` fields) |
| `0x14000bba0` | usage printer |
| `0x1400018b0` | EULA check, accept, and the RichEdit dialog |
| `0x140008170` | elevation check (token groups, Administrators SID enabled) |
| `0x140002240` | enable a named privilege (SeDebugPrivilege) |
| `0x14000b7d0` / `0x14000b570` | debugger discovery and SearchPath |
| `0x14000aad0` | symbol-path setup and the driver-backed live view |
| `0x140009830` | native live dump via `NtSystemDebugControl` |
| `0x140009be0` | dump dispatcher (native, mirror, or driver) |
| `0x14000bda0` | native-support check via `RtlGetVersion` (6.3 and above) |
| `0x140002320` | dump-file security descriptor builder |

### Native live dump (sub_140009830)

The function resolves `NtSystemDebugControl` from ntdll, creates the secured dump
file, fills a 64-byte `SYSDBG_LIVEDUMP_CONTROL` and calls command `37`
(`SysDbgGetLiveKernelDump`) with a 64-byte input buffer and no output buffer. The
recovered structure layout, confirmed with `static_assert` in the recreation:

| Offset | Field | Value used |
|---|---|---|
| `0x00` | Version | `1` |
| `0x04` | BugCheckCode | `0` |
| `0x08`..`0x20` | BugCheck params 1..4 | `0` |
| `0x28` | DumpFileHandle | the secured dump file |
| `0x30` | CancelEventHandle | a manual-reset event |
| `0x38` | Flags | `0x4` IncludeUserSpaceMemoryPages when a `-mp` process is set, `0x1` UseDumpStorageStack when `-md` |
| `0x3C` | AddPagesControl | `0x1` HypervisorPages when `-hvd` |

The dump file is opened with `CreateFileA` for
`GENERIC_READ | GENERIC_WRITE | DELETE | WRITE_DAC`, share
`READ | WRITE | DELETE`, disposition `CREATE_ALWAYS`, and attributes
`HIDDEN | SYSTEM` for the temp launch path or `NORMAL` for `-o`. The security
descriptor (sub_140002320) is a protected DACL that grants full access to the
SYSTEM account and the local Administrators group only, because the dump holds
kernel memory. On success the function prints `Saving live dump to <path>... done.`
and on failure `Saving live dump to <path>... failed (status %08X).`.

### Startup order quirk

The banner is always printed (LiveKd has no `-nobanner` switch). The EULA gate and
the elevation check both run before the argument loop, so `-?` only reaches the
usage printer after the elevation check passes. A non-elevated `livekd -?` therefore
prints the admin-rights message, not the usage. The recreation matches this order.

## Build

VS 2022 Community MSBuild, PlatformToolset v143, `stdcpp20`, Unicode, static CRT,
console subsystem. Both configurations build clean.

```
MSBuild src\LiveKd\LiveKd.vcxproj /p:Configuration=Debug   /p:Platform=x64 /m
MSBuild src\LiveKd\LiveKd.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

Output `src\LiveKd\x64\{Debug,Release}\LiveKd.exe`. The repo-wide
`Directory.Build.targets` applies the Release hardening and optimization flags. The
only build warning is the benign `D9025: overriding '/Ob2' with '/Ob3'` that every
tool in the repo emits from that shared targets file. The project is not added to
`sysinternals.sln`, per the task.

## Validation

- **Banner.** Non-elevated `livekd64.exe -accepteula` and the rebuild print the same
  first line `LiveKd v5.65 - Execute kd/windbg on a live system`, and the same
  `Livekd requires administrative rights. You must run it from an / elevated command
  prompt.` block. The only difference is the two credit lines in place of the
  original `Sysinternals - www.sysinternals.com` and copyright lines, as required.
- **Usage.** The usage block embedded in both binaries was extracted and diffed. All
  39 usage lines are byte-identical.
- **Live dump (`-o`).** Run elevated through the repo's silent elevated launcher as
  `LiveKd.exe -accepteula -o <scratch>.dmp`. It printed `Saving live dump to
  <path>... done.` (matching the original) and wrote a 13.76 GB file. The header is
  `PAGEDU64` (bytes `50 41 47 45 44 55 36 34`), the valid 64-bit kernel dump
  signature, followed by the live OS version fields (major `15`, minor `0x66BC` =
  26300, this Windows 11 build). The `NtSystemDebugControl(SysDbgGetLiveKernelDump)`
  call succeeded without bugchecking the system. The file DACL read back as
  `NT AUTHORITY\SYSTEM = FullControl` and `BUILTIN\Administrators = FullControl`
  only, confirming the protected security descriptor. A non-elevated session was
  denied read and delete on the file, which is the DACL working as intended. The
  scratch dump was deleted immediately after the header and DACL were confirmed.

## What needed the Debugging Tools for Windows

The Debugging Tools are installed on this box at
`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\` (`kd.exe` and `windbg.exe`
present), so the debugger discovery and launch paths are exercisable. The full
interactive handoff (launching `kd -z <dump>` against the snapshot and stepping
inside the live kernel) needs those tools and an interactive elevated console, so it
was validated by code inspection and by the discovery and argument-building logic
rather than by driving an interactive kd session in this batch run. The `-o`
dump-creation path, which is the safety-critical piece, was exercised live as
described above. Hyper-V debugging and the legacy driver path are out of scope and
were not exercised.
