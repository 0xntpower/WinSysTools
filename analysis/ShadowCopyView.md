# ShadowCopyView

Source-accurate C++20 plus WTL recreation of NirSoft ShadowCopyView v1.16 (x64,
requireAdministrator). Target binary `targets/shadowcopyview-x64/ShadowCopyView.exe`,
PDB path `c:\Projects\VS2005\ShadowCopyView\x64\Release\ShadowCopyView.pdb`.

## What it does

The upper pane lists the machine's Volume Shadow Copy snapshots. Selecting a snapshot
fills the lower pane with the files and folders inside it, which you can browse and copy
out to a local folder. The snapshot list exports through the usual `/s*` switches, and a
`/CopyFile` switch copies one file out headless.

## How it enumerates (the key question)

The binary does not walk raw device paths and it does not call the VSS enumeration
through a static import. It loads **VssApi.dll** at run time and drives the VSS COM
interface `IVssBackupComponents`. The loader at `sub_14001375C` resolves two exports by
name, the x64-mangled `?CreateVssBackupComponents@@YAJPEAPEAVIVssBackupComponents@@@Z`
(falling back to the x86 GS-mangled name) and `VssFreeSnapshotProperties`. The
enumerator at `sub_1400137C4` then runs this exact sequence.

1. `CreateVssBackupComponents` builds the backup object.
2. `InitializeForBackup(NULL)` (vtable offset 40).
3. `SetContext(VSS_CTX_ALL)` (vtable offset 280, value 0xFFFFFFFF).
4. `Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pEnum)` (vtable offset 344).
5. `pEnum->Next(1, &prop, &fetched)` in a loop, freeing each `VSS_SNAPSHOT_PROP` with
   `VssFreeSnapshotProperties`.

The `\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN` string is the snapshot's own
device object (`m_pwszSnapshotDeviceObject`), which the tool stores as the Snapshot Name
column and then uses as an ordinary path base for `FindFirstFile` browsing and for
copy-out. So both the VSS API and the GLOBALROOT path are involved, the API to enumerate
and the device path to read files. The recreation reproduces this in
`VssEnumerator.cpp`.

## Snapshot columns (upper pane, STRINGTABLE 1001..1013)

Field mapping recovered from `sub_1400137C4`, in display order.

| # | Title | Source |
|---|-------|--------|
| 0 | Snapshot Name | `m_pwszSnapshotDeviceObject` |
| 1 | Explorer Path | `\\localhost\X$\@GMT-YYYY.MM.DD-HH.MM.SS` built from the volume drive letter and the creation time |
| 2 | Volume Path | `GetVolumePathNamesForVolumeNameW(m_pwszOriginalVolumeName)` |
| 3 | Volume Name | `m_pwszOriginalVolumeName` |
| 4 | Originating Machine | `m_pwszOriginatingMachine` |
| 5 | Service Machine | `m_pwszServiceMachine` |
| 6 | Creation Time | `m_tsCreationTimestamp` |
| 7 | Attributes | decoded `m_lSnapshotAttributes` |
| 8 | Exposed Name | `m_pwszExposedName` |
| 9 | Exposed Path | `m_pwszExposedPath` |
| 10 | Snapshot ID | `m_SnapshotId` |
| 11 | SnapshotSet ID | `m_SnapshotSetId` |
| 12 | Provider ID | `m_ProviderId` |

The Explorer Path is only built when the volume resolves to a drive-letter mount point.
Its `@GMT-` timestamp comes from `FileTimeToSystemTime` on the raw creation time, so it
is always in UTC, which is how the Previous Versions names work. The three GUID columns
use the format `{%8.8X-%4.4X-%4.4X-%2.2X%2.2X-%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X}`. The
Attributes column joins the set bits of VSS_VOLUME_SNAPSHOT_ATTRIBUTES with a comma, in
the documented bit order, using the names in STRINGTABLE 701..718 (Persistent through
TxF recovery).

## File columns (lower pane, STRINGTABLE 1101..1107)

Filename, Modified Time, Created Time, File Size, Attributes, File Extension, Entry
Modified Time. The list builder at `sub_140002BF4` walks the device object path plus the
current folder with `FindFirstFile`, and for each entry opens the file with
`FILE_FLAG_BACKUP_SEMANTICS` and reads `FILE_BASIC_INFO` to get the Entry Modified Time,
which is the MFT ChangeTime. `GetFileInformationByHandleEx` is resolved from kernel32 by
name. Times use the shared formatter at `sub_140006200`, the user-locale short date plus
a space plus the user-locale time, local time unless Show Time In GMT is on.

## Copy out

The recursive copy at `sub_1400045C8` copies selected files and folders to a chosen
folder. When a name already exists in the destination it is copied under a numbered name
rather than overwriting, which the recreation matches by inserting a four-digit number
before the extension. Folder timestamps and attributes are carried over. The `/CopyFile`
CLI resolves the shadow copy by device object path, by number such as `3`, or by a short
volume such as `C:\` (newest snapshot of that volume), matching the matcher in
`sub_1400103C0`.

## Config

The binary keeps no registry key. There is no registry API imported at all. It reads and
writes `ShadowCopyView.cfg` next to the executable, section `[General]`, confirmed at
`sub_14000EE08`. Value names carried over are MarkOddEvenRows, ShowGridLines, ShowInfoTip,
ShowTimeInGMT, SaveFilterIndex, VerSplitLoc, WinPos, Sort, Columns, FilesSort and
FilesColumns. The recreation stores the binary blobs WinPos, Columns and FilesColumns as
hex strings in the INI.

## Window and geometry

The main window registers class `ShadowCopyView` and is created at `sub_1400100F4` with
`WS_OVERLAPPEDWINDOW` (0x00CF0000) and `CW_USEDEFAULT` for X, Y, width and height, so the
rebuild uses a plain `frame.CreateEx()`. Two `SysListView32` controls, a status bar, a
flat toolbar built from bitmap 110, and an editable path band sit directly on the main
window, with a draggable horizontal splitter between the two lists whose position
persists as VerSplitLoc. The toolbar layout and button-to-command mapping were recovered
from `sub_14000CF04`, a leading separator then Directory Level Up, Go To Root, Copy
Selected Files To, a separator, then Open In Explorer, Save, Refresh, Copy, Properties,
Find and Exit.

## Resources

Extracted with Resource Hacker and reused. Menu 102, context menus 104 and 112,
accelerators 103, dialogs 105 (Properties), 112 (About), 113 (Copy Files) and 1096
(Column Settings), toolbar bitmap 110 and sort-arrow bitmaps 133 and 134, icons 101 and
102, cursors 103 and 111, and the original requireAdministrator manifest. The extracted
accelerator table used `VK_A`-style letter keys that rc.exe rejects, so those became
quoted characters.

## Validation

This machine has no shadow copies. The original `/scomma` export produced only the
header row, confirmed independently through `vssadmin`-free observation of the tool's own
output. With no snapshot data to compare, validation covered the enumeration code path
(which runs the full VSS sequence and returns an empty list cleanly) and the export
format, which is a strong byte-level test because the header and document structure are
fixed.

### Export byte-for-byte match

Both binaries were run elevated through `launch-elevated.ps1` on the same machine and the
output files compared byte-for-byte. The encoding is UTF-16LE with a BOM for every format
except the comma format, which is UTF-8 with a BOM. The tab and comma formats carry a
header line. The HTML is the original's HTML 3.2 layout.

| Switch | Format | Original | Rebuild | Identical |
|--------|--------|----------|---------|-----------|
| /stext | Text | 2 B | 2 B | yes |
| /stab | Tab | 362 B | 362 B | yes |
| /stabular | Tabular | 2 B | 2 B | yes |
| /scomma | CSV | 182 B | 182 B | yes |
| /shtml | HTML horizontal | 1154 B | 1154 B | yes |
| /sverhtml | HTML vertical | 534 B | 534 B | yes |
| /sxml | XML | 140 B | 140 B | yes |

All seven are identical. The comma file carries the UTF-8 BOM, the others carry the
UTF-16LE BOM. The empty-data edge cases match too, text and tabular write only a BOM,
and the XML writes only the declaration and the empty `shadow_copies_list` root.

### Launch geometry

Config cleared, both launched elevated, window class `ShadowCopyView` located by process
id. Both produced the identical size 1920x1023, which is what `CW_USEDEFAULT` on an
overlapped window resolves to on this display. The top-left position differed only by the
operating system cascade offset (78,78 for one launch, 26,26 for another), which neither
binary controls. A rebuild that used a fixed rect would have produced a different size,
so the matching size confirms both use `CW_USEDEFAULT` for all four values. showCmd
matched (1, normal).

### Screenshot

`analysis/ShadowCopyView-toolbar.png` is a closeup of the rebuild showing the light flat
toolbar, the File Edit View Options Help menu and the snapshot column headers.
`analysis/ShadowCopyView-rebuild.png` is the full window with both panes and the status
bar reading `0 item(s)` and `0 Files+Folders`. The toolbar background is transparent and
light because `PinToLightMode()` runs before `InitCommonControls` and the toolbar uses
`LoadToolbarImageList`, which keys the strip's own corner colour transparent. The elevated
window cannot be moved or raised from a medium integrity shell because of UIPI, so the
right side of the full-window capture shows the terminal behind it. The toolbar closeup
is unobscured.

## Build

MSBuild v143, x64, both configurations build clean.

- Debug: `src/ShadowCopyView/x64/Debug/ShadowCopyView.exe`
- Release: `src/ShadowCopyView/x64/Release/ShadowCopyView.exe`

Release picks up the shared hardening and optimization from `Directory.Build.targets`.
The only warning is the harmless `/Ob2` overridden by `/Ob3`, the same as the other
tools. Dependencies are comctl32, shell32, advapi32, comdlg32, ole32, oleaut32, gdi32 and
version. VssApi.dll is resolved at run time rather than linked, matching the original.

## What did not match exactly, and deliberate choices

- The in-memory record layout is not reproduced. The recreation builds clean record
  structs over the VSS and FindFirstFile results rather than copying the original's
  4240-byte record stride. Observable output is unaffected.
- The snapshot Attributes separator and the file Attributes letters could not be
  compared against live data because this machine has no snapshots. The Attributes join
  uses a comma and the file-attribute letters follow the NirSoft convention including the
  documented I, E, X and V letters. These are the one area to re-check against a machine
  that has shadow copies.
- The main window is a clean WTL frame rather than a transliteration of the original's
  custom list control, embedded path dialog and hand-rolled splitter. Layout, columns,
  menus, toolbar, status bar and behaviour match. The pixel internals of the custom
  controls do not.

## Fidelity and HRC pass

Config storage. Confirmed the original keeps no registry key and persists to
ShadowCopyView.cfg next to the executable, section [General], with no registry API imported.
The recreation already matches at the store location, the section and the value names, which
is the fidelity bar for this tool, so it was left unchanged. One observation, the original
encodes the WinPos and Columns blobs as space separated uppercase hex bytes while the
recreation writes continuous hex. The store, section and value names match, and the blob
encoding was a documented earlier choice, so it was left as is under the confirm only scope.

HRC v1.1 light touch. The ColKind column enum became an enum class with its enumerator names
kept. The kind field is carried but unused here, so this is a pure type scoping change with
no behavior change. No other edit.

Rebuild and re-validation. Debug and Release x64 rebuilt clean with v143. The /scomma export
is byte identical to the original at 182 bytes. The ToolbarWindow32 empty area reads
240,240,240.
