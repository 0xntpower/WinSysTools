# PsInfo (Sysinternals PsInfo64.exe v1.79)

Source-accurate C++20 console reconstruction. Prints system information for a
local or remote computer: uptime, kernel version, product type and version,
service pack, build, registered owner and organization, IE version, system root,
processors, processor speed and type, physical memory, and video driver, with
optional installed hotfixes (`-h`), installed software (`-s`), and disk volume
information (`-d`). Reuses the shared PsTools command-line front end.

- Original: `targets/sysinternals/PsInfo64.exe` (x64, MSVC, static CRT, console, Unicode)
- Rebuild: `src/PsInfo/PsInfo.cpp` (banner and entry point) with the per-concern
  headers `PsInfoUtil.h` (base), `CommandLine.h` (tool-flag parse), `SubReports.h`
  (hotfix, software, disk), `SystemInfo.h` (gathering), and `InfoPrint.h` (output),
  all in a `psinfo` namespace, plus `src/common/PsTools.h` (read-only). Project
  `src/PsInfo/PsInfo.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The logic decompiled cleanly, no rea cross-check needed.

## What it does

```
Usage: psinfo [-h] [-s] [-d] [-c [-t delimiter]] [filter] [\\computer[,computer[,..]]|@file [-u Username [-p Password]]]
```

With no argument it prints the system information block for the local machine. A
positional filter limits the block to fields whose name starts with the filter.
`-h`, `-s`, and `-d` append the hotfix, software, and disk sections. `-c` prints
one comma-separated row with no labels, and `-t` overrides the delimiter. The
target can be the local system, remote computers, a domain wildcard, or a
response file. `-u` and `-p` supply alternate credentials. `-accepteula` and
`-nobanner` are the standard Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x140007D10 | OS gate, banner, EULA, parse, optional password prompt, run per computer |
| `sub_140005E20` | Argument parser (`-h -s -d -c -t`, filter, `\\computer`, `@file`, `-u`, `-p`) |
| `sub_1400079C0` | Usage |
| `sub_140004D20` | Per-computer driver: connect, collect, print |
| `sub_1400035C0` | Remote connect: optional LogonUser plus impersonate, then RegConnectRegistry |
| `sub_140004E70` | Collect every field from the registry, memory, hotfix, software, uptime |
| `sub_140006670` | Printer: field layout, product-type logic, disk, hotfix, software |
| `sub_140007730` | Recurse `Software\Microsoft\Updates`, one hotfix per key with a Description |
| `sub_140007520` | Split Internet Settings MinorVersion into `Internet Explorer - <token>` entries |
| `sub_1400048F0` | Local disks: GetLogicalDrives plus volume name, type, free bytes |
| `sub_140006440` | Legacy CPU-name lookup from processor_identifier family or model, fallback only |
| `sub_140006210` | WMI product-activation probe, result stored but never printed in v1.79 |
| `sub_1400017D0` | The wide printf used for every stdout line |

### Fields and their sources

The block is a fixed set of fields, each a 27-character label followed by the
value. All values are read with the wide registry APIs:

- Uptime, from the PDH `\System\System Up Time` performance counter, split into
  days, hours, minutes, seconds, each unit pluralized with an `s` when not 1.
- Kernel version, `ProductName` and `CurrentType` joined with a comma, under
  `Software\Microsoft\Windows NT\CurrentVersion`.
- Product type, derived from `ProductOptions\ProductType` (WinNT, ServerNT,
  LanmanNT), the `ProductSuite` multi-string, and the major and minor parsed from
  `CurrentVersion`. A client WinNT system with major >= 5 reads Professional.
- Product version, the `CurrentVersion` string.
- Service pack, the `CSDVersion` string with a leading `Service Pack ` stripped
  (default `0`), plus `SubVersionNumber`.
- Kernel build number, `CurrentBuildNumber`.
- Registered organization and owner, `RegisteredOrganization` and
  `RegisteredOwner`.
- IE version, `Internet Explorer\Version Vector\IE`, or `Unknown` when empty.
- System root, `SystemRoot`.
- Processors, `Session Manager\Environment\number_of_processors`.
- Processor speed, `CentralProcessor\0\~MHz` rounded up to the nearest 5, printed
  as MHz below 1000 and as `d.d GHz` at or above.
- Processor type, `CentralProcessor\0\ProcessorNameString` with leading spaces
  dropped, or the raw `processor_identifier` when that string is absent.
- Physical memory, `GlobalMemoryStatusEx` total physical rounded up to an even MB.
- Video driver, `DriverDesc` under the first display-class subkey `00NN`.

### Hotfix, software, disk

`-h` collects three sources into one list, in order: a recursive walk of
`Software\Microsoft\Updates` keyed by the `Description` value, the subkeys of
`Windows NT\CurrentVersion\Hotfix` keyed by subkey name with a substring dedup
against entries already seen, and the Internet Settings `MinorVersion` split on
the semicolon into `Internet Explorer - <token>` entries. Each row shows the key
last-write date formatted with `GetDateFormatW`, or `n/a` when there is no time.

`-s` enumerates `Windows\CurrentVersion\Uninstall`, reading `DisplayName` and
`DisplayVersion`, and keeps the list sorted ascending by name with an ordinal
compare, inserting before the first element that is not less than the new one.

`-d` walks the logical drives and reads the volume label, file-system name, drive
type, total size, and total free bytes. The size columns switch between GB, MB,
and KB by magnitude. The free percentage is free over total.

### The CSV and filter modes

`-c` sets one flag that suppresses every label, replaces the line header with the
bare computer name, and swaps the per-field terminator from a newline to the
delimiter. The same field format strings serve both modes, so the kernel-version
value with its internal comma splits into two CSV fields exactly as the original
does. The positional filter prints a field only when its name starts with the
filter text compared case-insensitively over the filter length.

### The shared PsTools front end

Banner, EULA gate, target-list parsing, target iteration, local identity, and
error text are reused from `src/common/PsTools.h` unchanged. PsInfo strips its own
`-h -s -d -c -t` flags first, then hands the rest to the shared parser, whose
account slot carries the field filter. The EULA flag is written to
`HKCU\Software\Sysinternals\PsInfo\EulaAccepted` as DWORD 1, which was confirmed
against the original.

## Validation

Original and rebuild (x64 Release) run with the same arguments and `-nobanner`,
stdout compared. The volatile fields (uptime, free memory, free disk) are
normalized before the compare, everything else matches byte for byte. No license
key or product id is shown by this tool, and the registered owner value was not
recorded here.

| Arguments | stdout result |
|-----------|---------------|
| (default) | identical except the uptime value |
| `-h` | identical after normalizing uptime |
| `-s` | identical after normalizing uptime (363 applications, same order and format) |
| `-d` | identical after normalizing uptime and the free columns (totals, type, format, label all match) |
| `-c` | identical after normalizing the uptime field |
| `-c -t "\t"` | tab-delimited row, matches |
| `service` filter | identical (single Service pack field) |
| `proc` filter | identical (Processors, Processor speed, Processor type) |

The stable sample block reproduced exactly (owner value masked here):

```
System information for \\NORT:
Uptime:                    5 days 18 hours 51 minutes 18 seconds
Kernel version:            Windows 10 Pro for Workstations, Multiprocessor Free
Product type:              Professional
Product version:           6.3
Service pack:              0
Kernel build number:       26300
Registered organization:   
Registered owner:          <masked, matches original>
IE version:                9.0000
System root:               C:\WINDOWS
Processors:                16
Processor speed:           3.6 GHz
Processor type:            AMD Ryzen 7 3700X 8-Core Processor             
Physical memory:           65448 MB
Video driver:              
```

Disk header and totals matched, for example
`Volume Type       Format     Label                      Size       Free   Free`
and `    C: Fixed      NTFS                            1675.82 GB` with only the
free columns varying. The single hotfix entry
`n/a           Internet Explorer - 0` comes from the Internet Settings
MinorVersion source and matched exactly.

### Re-validation after the unit refactor

The single `PsInfo.cpp` was split into the `PsInfoUtil.h`, `CommandLine.h`,
`SubReports.h`, `SystemInfo.h`, and `InfoPrint.h` units, behaviour unchanged, then
rebuilt x64 Debug and Release clean. Original and rebuild were rerun with
`-nobanner -accepteula` and stdout compared again:

- default, `-h`, `-s`: identical once the uptime line is normalized (`-s` still 363
  applications, same order and format).
- `-d`: identical once the uptime line and the volatile free columns are normalized,
  with the totals, type, format, and label all matching.
- `-?`: the usage body from the `returns information` line through the `-nobanner`
  line is byte-for-byte identical. Only the banner block differs, which is deviation
  4 (the rebuild carries its own banner rather than the original copyright line).

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsInfo\PsInfo.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsInfo\PsInfo.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Deviations

1. **Remote disk and uptime over PSINFSVC**. The original deploys a bundled
   PSINFSVC helper to the remote `ADMIN$`, starts it through the SCM, and reads a
   data block from its `\pipe\psinfsvc` named pipe for the parts that cannot come
   from the remote registry. That helper binary is out of scope, so the rebuild
   reproduces the local path fully and the remote registry connect (RegConnect
   plus impersonation) for the registry-based fields, and leaves the service
   auto-deploy unreproduced, matching how PsGetSid handles its own helper service.
   Validation was local only, as allowed.
2. **Legacy CPU-name table**. `sub_140006440` maps a processor_identifier family
   and model to a hardcoded CPU name, a path reached only when
   `ProcessorNameString` is absent (pre-2001 CPUs). The rebuild falls back to the
   raw `processor_identifier` instead. Not exercised on supported hardware.
3. **Product activation probe**. `sub_140006210` queries
   `Win32_WindowsProductActivation` over WMI and stores the result, but v1.79
   never prints it, so no activation, expiration, or product-id line exists in
   this version and the probe is omitted.
4. **Usage banner**. The original reprints the banner from inside its usage
   routine even under `-nobanner`. The rebuild prints the banner from main, which
   matches the normal path exactly, so a usage error combined with `-nobanner`
   omits the banner. The program name in the usage line is the fixed `psinfo`,
   matching the original.
5. **OS version gate**. The original "Windows XP or higher" check is dropped as
   always true on supported systems.
6. **Banner source**. The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.
7. **Fixed buffers**. The original reads most registry values into 260-character
   buffers. The rebuild reads them in full, a fidelity improvement that differs
   only for a value longer than that, which none of these fields reach here.
8. **EULA interactive fallback**. The rebuild uses the shared console Y/N prompt.
   The GUI dialog the original shows on an interactive desktop is not reproduced,
   which a console tool does not need.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are consolidated
separately. No commit was made.
