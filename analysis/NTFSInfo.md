# NTFSInfo

Source-accurate C++20 console recreation of the Sysinternals **NtfsInfo v1.2**
utility. Original binary `targets/sysinternals/ntfsinfo64.exe` (x64,
md5 c2562de6fad25d0529b1feef5b15e43f). Recreation in `src/NTFSInfo/`, builds to
`ntfsinfo64.exe`.

This is a CLI tool, not a WTL GUI, so the toolbar, light-mode, and window
geometry conventions in the reconstruction guide do not apply.

## What it does

Dumps NTFS volume information for one drive. It opens a raw handle to the volume
(`\\.\C:`), issues `FSCTL_GET_NTFS_VOLUME_DATA` and prints the volume size,
cluster and sector sizes, the MFT location, size and zone, and the sizes of the
NTFS metadata files.

Usage: `ntfsinfo64 [-nobanner] [-accepteula] <drive letter>`

## Reverse engineering

IDA idalib MCP was the primary decompiler. Key functions:

| Address | Role |
|---------|------|
| `0x140002a40` `main` | arg handling, drive parse, report printing |
| `0x140001050` | strips `-nobanner`, prints banner from own version resource |
| `0x140001fe0` | strips `-accepteula`, runs the EULA gate |
| `0x1400021b0` | EULA accept, writes `EulaAccepted` under `Software\Sysinternals\NTFSInfo` |
| `0x140002910` `Initialize` | resolves `ntdll!NtFsControlFile` |
| `0x140002800` | opens the volume, calls NtFsControlFile with FSCTL 0x90064 |
| `0x140002700` | lists metadata files with FindFirstFile |
| `0x1400015e0` | reads one version-resource field for the banner |

### Manifest

The original manifest is **asInvoker**, not requireAdministrator. It does not
self-elevate. Opening the raw volume handle needs Administrator, so the caller
is responsible for running it from an elevated context. Non-elevated it prints
`Error opening volume: Access is denied.` The recreation carries the same
asInvoker level.

### Volume query

`CreateFileA("\\.\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
OPEN_EXISTING, 0, NULL)` then `NtFsControlFile(h, 0, 0, 0, &iosb, 0x00090064,
0, 0, &buf, 96)`. The output buffer is a 96-byte `NTFS_VOLUME_DATA_BUFFER`. The
original uses the native `NtFsControlFile` rather than `DeviceIoControl`, which
is why its failure path reports NTSTATUS names (`STATUS_INVALID_PARAMETER`,
`STATUS_INVALID_DEVICE_REQUEST`, `STATUS_ALREADY_COMMITTED`,
`STATUS_BUFFER_TOO_SMALL`, else `0x%08x`). The recreation resolves and calls the
same native entry so the error reporting matches.

### Field mapping

Each printed line maps to an `NTFS_VOLUME_DATA_BUFFER` member. Arithmetic is
integer division, left to right, as the binary does it.

| Line | Source | Formula |
|------|--------|---------|
| Volume size MB | NumberSectors, BytesPerSector | sectors * bps / 0x100000 |
| Total sectors | NumberSectors | |
| Total clusters | TotalClusters | |
| Free clusters | FreeClusters | |
| Free space MB (pct) | FreeClusters, BytesPerCluster | free * bpc / 0x100000, 100 * free / total |
| Bytes per sector | BytesPerSector | |
| Bytes per cluster | BytesPerCluster | |
| Bytes per MFT record | **ClustersPerFileRecordSegment** | see quirk |
| Clusters per MFT record | ClustersPerFileRecordSegment | |
| MFT size MB (pct) | MftValidDataLength, BytesPerCluster, TotalClusters | mft / 0x100000, 100 * mft / bpc / total |
| MFT start cluster | MftStartLcn | |
| MFT zone clusters | MftZoneStart, MftZoneEnd | start - end |
| MFT zone size MB (pct) | MftZoneEnd, MftZoneStart, BytesPerCluster | (end - start) * bpc / 0x100000, 100 * (end - start) / total |
| MFT mirror start | Mft2StartLcn | |

No volume serial number is printed. The structure carries one at offset 0 but
`main` never emits it.

### The MFT-record quirk

Confirmed in the disassembly at `0x140002c3a` and `0x140002c49`. Both the
`Bytes per MFT record` line and the `Clusters per MFT record` line load the same
stack slot, which is `ClustersPerFileRecordSegment` at struct offset 0x34. The
`BytesPerFileRecordSegment` member at offset 0x30 is never read. On modern NTFS
`ClustersPerFileRecordSegment` is 0 because an MFT record is smaller than a
cluster, so both lines show 0 and the true 1024-byte record size is not shown.
The recreation reproduces this verbatim so the output diffs clean.

### Metadata files

`0x140002700` walks an 11-entry table of names, builds `%c:\%s`, and runs
`FindFirstFileA`. Found entries print as `%-11s\t%I64d bytes`. The names in
order are `$mft`, `$mftmirr`, `$logfile`, `$volume`, `$attrdef`, `$bitmap`,
`$boot`, `$badclus`, `$quota`, `$badclust`, `$upcase`. On a live system volume
these paths do not resolve through the normal namespace, so `FindFirstFile`
fails on every entry and the section stays empty. The recreation does the same
and produces the same empty section.

### Banner and EULA

The banner reads the binary's own version resource with
`GetFileVersionInfo` and `VerQueryValue`, then prints
`\n%s v%s - %s\n%s\n%s\n\n` from InternalName, FileVersion, FileDescription,
LegalCopyright, CompanyName. The recreation embeds a matching VERSIONINFO and
reads it the same way, so the banner is identical and the EXE properties match.

The EULA flag lives at `HKCU\Software\Sysinternals\NTFSInfo\EulaAccepted`
(REG_DWORD). `-accepteula` or `/accepteula` writes it, an existing nonzero value
satisfies the check, and both switches are stripped from argv before the drive
argument is read.

## Deviations from the original

- **EULA dialog.** When the flag is absent and `-accepteula` was not given, the
  original raises a RichEdit GUI license dialog. This console recreation asks
  for `-accepteula` and exits instead of building that dialog. The registry flag
  and both switches behave the same. This is the one behavioural simplification.
- Everything else matches, including the MFT-record quirk and the empty
  metadata-file section on a live volume.

## Build

New x64 console vcxproj, Application, SubSystem Console, v143, stdcpp20,
Unicode, static CRT (MultiThreadedDebug for Debug, MultiThreaded for Release via
`Directory.Build.targets`). Not added to any solution.

```
MSBuild src\NTFSInfo\NTFSInfo.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\NTFSInfo\NTFSInfo.vcxproj /p:Configuration=Release /p:Platform=x64
```

Both configurations build clean. The only warning is the repo-wide D9025
`/Ob2` over `/Ob3` note from the shared hardening targets.

## Validation

Original and rebuild both run elevated on `c:` through
`.tools/launch-elevated.ps1`, output captured and diffed.

Raw diff, 31 lines each, one differing line:

```
L12: ORIG=[Free clusters          : 153479616]  REBUILD=[Free clusters          : 153479614]
```

Free clusters is volatile and changes between runs. After normalizing the
volatile fields (Free clusters, Free space, MFT size valid-data length), the two
outputs are **identical**. Every stable field matches exactly, including volume
size, total sectors, total clusters, bytes per sector and cluster, MFT start and
mirror, MFT zone layout, and the empty metadata-file section.

Non-elevated checks also match the original: `/?` prints banner and usage,
`-nobanner c:` prints `Error opening volume: Access is denied.`, a non-letter
drive prints `illegal drive: <c>`.
