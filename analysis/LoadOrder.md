# LoadOrder (console) - reverse engineering and validation

Original: `targets/sysinternals/LoadOrdC64.exe`, Sysinternals Loadord v1.02, "Startup
order viewer", Copyright (C) 1998-2021 Mark Russinovich. x64 console build.
Recreation: `src/LoadOrder/` -> `LoadOrdC64.exe`, C++20, built Unicode with the static CRT.

This is the console companion of the LoadOrder GUI. It shows the order in which
Windows loads device drivers and services at boot. The whole computation runs off
the registry. Nothing goes through the Service Control Manager.

## Engines

IDA idalib MCP was the primary decompiler (session opened on the original, closed
after). The enumeration, the ordering, the registry keys and the record layout
were all read from the Hex-Rays output. The output format and the ordering were
cross-checked against a live run of the original, byte for byte.

## Function map (IDA)

| Address | Role |
|---------|------|
| `main` | setlocale, banner, EULA gate, enumerate, header, then three ordering passes |
| `sub_1400013B0` | enumerate `HKLM\SYSTEM\CurrentControlSet\Services`, one record per subkey |
| `sub_140001820` | order one start type by `ServiceGroupOrder\List` then `GroupOrderList` |
| `sub_140001000` | the leftover pass, services whose group is not in the List |
| `sub_140001D10` | the `vfprintf` wrapper every row goes through |
| `sub_140001E80` | banner, built from the version resource, routed to stdout or stderr |
| `sub_140002F80` / `sub_140003220` | EULA gate and the HKCU flag |

## Data source and record layout

`sub_1400013B0` opens `HKLM\SYSTEM\CurrentControlSet\Services`, walks the subkeys
with `RegEnumKeyA` from index 0, and for each subkey reads five values with
`RegQueryValueExA`. It stores a 48-byte record into a fixed array of 1000 entries.

| Field | Source value | Absent default |
|-------|--------------|----------------|
| name | the subkey name from `RegEnumKeyA` | always present |
| DisplayName | `DisplayName` | null, column blank |
| ImagePath | `ImagePath` | null, column blank |
| Group | `Group` | the literal `n/a` |
| Start | `Start` (first dword of the value) | `0xDEADBEEF` |
| Tag | `Tag` (first dword of the value) | `0xDEADBEEF` |
| flag | printed marker, starts at 0 | - |

The tool is ANSI end to end. It reads values with the `...A` functions and prints
with the narrow CRT, so the columns carry the raw stored value. DisplayName stays
a resource reference like `@%SystemRoot%\system32\drivers\Wdf01000.sys,-1000`, and
ImagePath stays the stored path. The recreation uses the same `...A` calls, which
is what makes the output match to the byte.

## Ordering algorithm

`main` enumerates once, writes the header, then calls the ordering routine three
times, for start type 0, 1, 2. Only Boot, System and Automatic are listed. Demand
(Manual) and Disabled are never printed.

For one start type:

1. Read `HKLM\SYSTEM\CurrentControlSet\Control\ServiceGroupOrder\List`, a
   REG_MULTI_SZ of group names in load order.
2. For each group name in that list, read the value of the same name under
   `HKLM\SYSTEM\CurrentControlSet\Control\GroupOrderList`. That binary blob is
   `[count][tag0][tag1]...`. The passes become each listed tag in order, followed
   by a catch-all pass marked `0xDEADBEEF`.
3. For each pass, scan all records in enumeration order. A record prints when its
   start equals the current start type, its group matches the list name under a
   case-insensitive compare, the pass is the catch-all or the record tag equals the
   pass tag, and the record has not printed yet. Printing sets the flag.
4. After the listed groups, a final leftover pass prints every remaining record of
   this start type whose group was not in the list, each shown with its own group
   name and a trailing `*`.

The case-insensitive group compare is why the Group column shows the service's own
casing, for example both `SCSI Miniport` and `SCSI miniport`, rather than the list
spelling.

## Columns and the two asterisk markers

Format string, header and data alike:

```
%-12s %-25s %-10s %-30s %-50s %-40s\n
```

Widths 12, 25, 10, 30, 50, 40 with one separating space. A value wider than its
column is not cut, it pushes the following columns to the right. The header row is
the same format applied to the column titles, including the original's `Service/Devide`
misspelling, which is reproduced as-is.

The Group column carries a trailing `*` when the group is not in the List, meaning
the service was placed by the leftover pass.

The Tag column shows:
- `n/a*` when the service has no Tag value.
- the number alone when the tag was placed by the group's explicit `GroupOrderList`
  order.
- the number with a trailing `*` when the tag fell through to the catch-all pass,
  that is, the tag was not named in the group's order.

Both markers were confirmed against the live output. Seven rows on the test machine
carry a numeric-tag asterisk inside a listed group, for example `ADP80XX` with tag
`210*` under `SCSI Miniport`, which is exactly the catch-all case.

## Banner and EULA

The banner is `\n%s v%s - %s\n%s\n%s\n\n` filled from the version resource, giving

```
Loadord v1.02 - Startup order viewer
Copyright (C) 1998-2021 Mark Russinovich
Sysinternals - www.sysinternals.com
```

It goes to stdout when stdout is a real console and to stderr otherwise, so a
redirected stdout stays table-only. `-nobanner` and `/nobanner` suppress it.

The EULA gate stores acceptance in `HKCU\Software\Sysinternals\LoadOrder` as the
REG_DWORD `EulaAccepted`. `-accepteula` or `/accepteula` sets it and runs headless.
On this machine the flag was already 1, so the original runs without a prompt.

Manifest: `asInvoker`, `uiAccess=false`, trustInfo only. The recreation embeds the
same manifest. Read access to the Services and Control keys needs no elevation.

## Deviation

On first run with the flag unset, the original pops a GUI license dialog through
`DialogBoxIndirectParamA`, shared with the GUI build, plus special console or
auto-accept paths for IoT Core and Nano Server. That dialog is out of scope for a
clean console recreation. This build follows the same HKCU flag and the same
`-accepteula` switch as the other Sysinternals CLI recreations in this repo. When
the flag is unset and no switch is passed, it declines to a one-line message on
stderr rather than drawing a dialog. Registry key, value name and type all match.

## Build

- `src/LoadOrder/LoadOrder.vcxproj`, Application, SubSystem Console, v143, stdcpp20,
  Unicode, static CRT. `TargetName` is `LoadOrdC64`.
- Release hardening and speed flags come from the repo `Directory.Build.targets`.
- Built both configurations with VS 2022 Community MSBuild, x64. Both link clean.
  The only warning is the benign `/Ob3` overriding `/Ob2` from the shared targets.

## Validation

Ran the original and the Release rebuild with stdout captured, and diffed.

| Check | Result |
|-------|--------|
| Release stdout vs original | byte-identical, 52656 bytes, 279 lines |
| Debug stdout vs original | byte-identical |
| Banner on stderr vs original | byte-identical, 121 bytes |
| Ordering stable between back-to-back runs | stable |
| `-nobanner`, `-accepteula`, `/?` | stdout unchanged, table-only |
| Embedded manifest | `asInvoker`, matches the original |

No fields needed normalizing. The driver and service load order is stable on this
machine between runs, and the rebuild reproduces it exactly.
