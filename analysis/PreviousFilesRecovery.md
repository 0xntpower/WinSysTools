# PreviousFilesRecovery -- reverse-engineering notes

Target: `targets/previousfilesrecovery-x64/PreviousFilesRecovery.exe` -- NirSoft
PreviousFilesRecovery v1.10, x64 PE, Unicode, VC2005-era CRT (`__wgetmainargs`,
`_initterm`). md5 `9e4c902e52d513e1437c599953ef7e1e`, sha256
`2c749ebe67bfb2496b47d2bcd8b05e32134e71bcc51d0b1bf63b2dd668f56b5e`. Image base
0x140000000, 332 functions, 246 strings. Engine used -- IDA idalib (Hex-Rays),
session over `PreviousFilesRecovery.exe.i64`. Build path string left in the binary
confirms `VS2005\PreviousFilesRecovery\x64\Release`.

## What the tool does

It scans the Volume Shadow Copies of a local drive and lists previous versions of
existing files, deleted files and deleted folders found inside them, and recovers a
selected item by copying it out of the shadow copy into a chosen folder.

## Entry and startup (wWinMain @0x140010488)

- Loads comctl32, resolves and calls `InitCommonControlsEx`, then `CoInitialize`.
- `sub_14000FFA4` builds the app context, `sub_1400075BC` parses the command line.
- If `/savelangfile` is present it writes `<module>_lng.ini` and exits.
- Else `sub_140008828` reads the language file if one exists, then, when the first
  argument count is above one, `sub_14001027C` runs the command-line export and the
  process exits without a window.
- Otherwise `sub_14000FEC8` registers the window class and creates the main window,
  then the message loop runs with `TranslateAcceleratorW` plus `IsDialogMessageW` for
  the embedded search panel.

The CLI export handler `sub_14001027C` recognises the switches `/stext` `/shtml`
`/sverhtml` `/sxml` `/stab` `/stabular` `/scomma` (internal format codes 1,5,6,7,2,3,4),
runs the scan with the stored options, applies `/sort`, and writes the report to the
file named after the switch.

## Window geometry (sub_14000FEC8)

`CreateWindowExW(0, "PreviousFilesRecovery", "PreviousFilesRecovery", 0xCF0000,
CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, ...)`. Style 0xCF0000 is WS_OVERLAPPEDWINDOW.
X and Y are CW_USEDEFAULT, so the first-launch position is assigned by the window
manager cascade and the size is a fixed 640 by 480. The recreation passes
`RECT rc = { CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT + 640, CW_USEDEFAULT + 480 }`
to `CreateEx`. Saved placement (WinPos) overrides on later launches.

## Configuration -- an INI file, not the registry

There are no `Reg*` imports. The tool reads and writes an INI next to the executable
named `PreviousFilesRecovery.cfg`, or the path given by `/cfg`, all under the
`[General]` section. Value names recovered from `sub_140001F88` and `sub_14000E7E0`:

| Value | Type | Default |
|-------|------|---------|
| MarkOddEvenRows | dword | 0 |
| ShowGridLines | dword | 0 |
| ShowInfoTip | dword | 1 |
| ShowTimeInGMT | dword | 0 |
| SaveFilterIndex | dword | 0 |
| BaseFolder | string | empty |
| FilesWildcard | string | `*` |
| SubfoldersDepth | dword | 0 (Unlimited) |
| FindPreviousFiles | dword | 1 |
| FindDeletedFiles | dword | 1 |
| FindDeletedFolders | dword | 1 |
| MultipleCopiesMode | dword | 2 |
| WinPos | binary | window placement |
| Columns | binary | width plus index per column |
| Sort | dword | signed sort column |

The recreation keeps the same names and writes the binary blobs (WinPos, Columns) as
hex strings so the INI stays plain text.

## The shadow-copy scan

### Snapshot enumeration (sub_140012FFC, loader sub_140012F94)

It loads `VssApi.dll` and resolves `CreateVssBackupComponents` by its mangled export
name (the x64 form `?CreateVssBackupComponents@@YAJPEAPEAVIVssBackupComponents@@@Z`),
then drives the VSS COM API directly, so this is the COM path, not raw device-path
guessing:

1. `CreateVssBackupComponents` builds the `IVssBackupComponents` object.
2. `InitializeForBackup(NULL)`.
3. `SetContext(VSS_CTX_ALL)` (0xFFFFFFFF).
4. `Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pEnum)`.
5. The `IVssEnumObject::Next` loop reads each `VSS_SNAPSHOT_PROP`, keeping the
   device object path `m_pwszSnapshotDeviceObject` (the
   `\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN` string), the original volume name,
   and the creation time. `GetVolumePathNamesForVolumeNameW` maps the volume to a drive
   letter, and the snapshots are filtered to the base folder's drive.

The recreation reproduces this in `ShadowCopy.cpp` with the same dynamic load and the
same COM calls.

### File walk and classification (sub_140011168, sub_140010E10)

For the base folder the live files that match the wildcard are gathered once, then for
each shadow copy the same folder inside the snapshot is opened at
`deviceObject + relativePath` (the relative path is the live folder with the `X:`
drive prefix removed). Each snapshot entry is matched by name against the live files:

- Matched and it is a file and the size or the last-write time differs -> Older File
  Version, carrying the current and the previous size, time and attributes.
- Not matched and it is a file -> Deleted File.
- Not matched and it is a folder -> Deleted Folder.

The live tree is then recursed into subfolders up to the depth limit, where the limit 0
means unlimited and N recurses while N is greater than the current depth plus one.

### Scan options

The three checkboxes gate the three item kinds (FindPreviousFiles, FindDeletedFiles,
FindDeletedFolders at context offsets 1052, 1056, 1060). The scan-mode combo stores
`MultipleCopiesMode` as the combo index plus one, confirmed by the code enabling the
shadow-copy picker only when the value equals 4:

1. Find all file copies.
2. Find all file copies with different size or write time (default).
3. Find only the latest file version.
4. Find only files in the selected shadow copy.

The binary's per-snapshot compare always requires a size or time difference, which is
mode 2. The recreation treats mode 1 as including identical copies, mode 2 as the
difference test, mode 3 as mode 2 followed by keeping only the newest previous version
of each file, and mode 4 as the difference test limited to the chosen snapshot. The
mode-1 and mode-3 refinements are inferred from the option strings, since this machine
carries no snapshots to confirm them against.

## Columns and cell formatting

Eleven columns, internal index equal to the string id minus 1001, field mapping taken
from the column-text builder `sub_1400024CC`:

| # | Title | Field |
|---|-------|-------|
| 0 | Filename | snapshot file name |
| 1 | Folder | live folder path |
| 2 | Item Type | Older File Version / Deleted File / Deleted Folder |
| 3 | Current Modified Time | current file time, Older File Version only |
| 4 | Previous Modified Time | snapshot file time |
| 5 | Current File Size | current file size, Older File Version only |
| 6 | Previous File Size | snapshot file size, blank for a folder |
| 7 | Current File Attributes | current attributes, Older File Version only |
| 8 | Previous File Attributes | snapshot attributes |
| 9 | Shadow Copy Name | the GLOBALROOT device object path |
| 10 | Shadow Copy Time | snapshot creation time |

Formatting matches the binary. Times use `GetDateFormatW` with the short-date flag plus
a space plus `GetTimeFormatW`, converted to local time unless Show Time In GMT is on
(`sub_140002428`, `sub_140006D24`). Sizes use `GetNumberFormatW` with no decimals and
the locale grouping (`sub_140006BE0`). Attributes are letters in the order A C R H S D T
I E X V keyed off the attribute bits (`sub_140002114`).

## Export formats

Seven save formats behind File > Save and the `/s*` switches, STRINGTABLE 501-507 order.
The comma report is UTF-8 with a BOM, every other report is UTF-16LE with a BOM, matching
the encoding rule in `sub_14000B700`. Structure recovered by running the original and
matched exactly:

- `/scomma` has a header row of titles joined by a comma with no trailing comma.
- `/stab` has a header row with every field followed by a tab, so a trailing tab.
- `/stabular` has no header, fixed-width columns.
- `/stext` writes title and value blocks with the title padded to 18.
- `/sxml` opens with `<?xml version="1.0" ?>` and a `files_list` root, lowercased
  underscore element names.
- `/shtml` and `/sverhtml` use the HTML 3.2 doctype, a bordered table, `<th>` header
  cells, and the "Created by using nirsoft.net" footer.

## Resources

Extracted with Resource Hacker. Menu 102 (File, Edit, View, Options, Help), context menu
104, the single-item Stop menu 112, accelerators 103, toolbar bitmap 110 (112 by 16,
24-bit, 7 images), dialogs 105 Properties, 107 Exception, 111 the embedded search panel,
112 About, 113 Recover Files, 1096 Column Settings, the string tables, the icons, and the
original `requireAdministrator` manifest reused verbatim. The .rc needed the two includes
prepended, the letter accelerators quoted, and the dialog FONT weight written as a number.

## Build

VS 2022 MSBuild, PlatformToolset v143, x64, Debug and Release both build clean. Release
hardening and optimization come from the repo `Directory.Build.targets`. VSS is reached
by a runtime load of VssApi.dll, so no import library is linked. The one new link library
is uxtheme for `SetWindowTheme`.

## Validation

This machine has no Volume Shadow Copies (`vssadmin list shadows` reports none), so the
scan returns an empty list and no file content is ever touched. The scan code path and
the export format were validated instead, as the task directs. All launches of the
original and the rebuild went through the elevated launcher.

- Export format, original vs rebuild, byte-for-byte with an empty result set
  (`Compare-Object` over the raw bytes):

  | Switch | Original | Rebuild | Identical |
  |--------|----------|---------|-----------|
  | /scomma | 195 | 195 | yes |
  | /stab | 388 | 388 | yes |
  | /stabular | 2 | 2 | yes |
  | /sxml | 108 | 108 | yes |
  | /shtml | 1142 | 1142 | yes |

  The CSV and tab files carry the full 11-title header, so the column listing structure
  is confirmed even without data rows.

- Scan code path: launched elevated, the VSS enumeration runs, finds no snapshots, and
  the panel reports "Cannot find shadow copies for this drive !" without error.

- Launch geometry, config cleared, both launched elevated: the original and the rebuild
  both report GetWindowRect 640 by 480 and GetWindowPlacement showCmd 1 with a normal
  size of 640 by 480. The X and Y differ between runs because both pass CW_USEDEFAULT and
  the window manager assigns the cascade position, which is the intended behaviour.

- Top strip, light flat toolbar: `analysis/pfr-toolbar.png`, cropped to the title, menu
  and toolbar so no scan data is visible. The full window matches the original layout, a
  flat toolbar over the embedded search panel (Base Folder, the browse button, Subfolders
  Depth, Files Wildcard, the three checkboxes, the scan-mode combo, the shadow-copy combo,
  the Start button) over the result ListView over the status bar.

## Differences from the original

- The toolbar uses the shared `LoadToolbarImageList` helper and the flat style, as the
  other x64 tools do. On a dark-mode system an elevated process paints a themed-dark band
  behind the flat toolbar, because `SetPreferredAppMode(ForceLight)` does not reach the
  toolbar there. The already-built FileTypesMan shows the same dark band when it is
  launched elevated, so this is an environment artifact of the elevated toolbar rather
  than a one-tool defect. The recreation drops the visual style on the toolbar with
  `SetWindowTheme` and fills the frame with the button-face colour in `OnEraseBkgnd`, so
  the transparent flat band and the strip's masked-out pixels read light and the toolbar
  matches the original's light flat look.
- The scan runs on the UI thread with a wait cursor rather than on a background thread
  with a live progress box. The status line still shows a running count.
- The config binary blobs are stored as hex strings in the INI rather than the original's
  packed encoding, since config compatibility with the original binary is not required and
  the value names are the ones that matter.

## Fidelity and HRC pass

Config storage. Confirmed the original keeps no registry key and persists to
PreviousFilesRecovery.cfg next to the executable, section [General], with no registry API
imported. The recreation already matches at the store location, the section and the value
names, so it was left unchanged. The same blob encoding observation as ShadowCopyView
applies, the original writes space separated uppercase hex bytes while the recreation writes
continuous hex. It was left as is under the confirm only scope.

HRC v1.1 light touch. The ColKind column enum became an enum class with its enumerator names
kept, and the one alignment comparison site was qualified. No behavior change.

Rebuild and re-validation. Debug and Release x64 rebuilt clean with v143. The /scomma export
is byte identical to the original at 195 bytes. The ToolbarWindow32 empty area reads
240,240,240.

## MainFrame header and body split

The `CMainFrame` window-class method bodies were moved out of `MainFrame.h` into a new
`MainFrame.cpp`, matching the WinObj golden template (commit 4a3adc0). Behaviour-preserving
code motion plus the HRC clang-format pass, no logic change. The header now carries only the
class shape, its members, the WTL macros, the message map and the method declarations. The
`CSearchPanel` helper dialog stays inline in the header, the way WinObj keeps its `ScopedWait`
helper. The out-of-line definitions drop `override` and `static` on the signature and keep it
on the declaration, the WTL handler signatures keep the trailing `BOOL&`, and the flat-toolbar
light fix (SetWindowTheme in CreateToolbar, OnEraseBkgnd filling the frame with button-face)
is byte identical.

- `MainFrame.h`: 628 lines before, 252 after.
- `MainFrame.cpp`: new, 679 lines.
- Added `MainFrame.cpp` to the vcxproj ClCompile group.
- clang-format `--style=file` applied to both new files, exit 0.
- Debug x64 and Release x64 both rebuilt clean with v143. The only warnings are the
  pre-existing D9025 `/Ob2` to `/Ob3` override from the shared `Directory.Build.targets`,
  which every tool emits and which this change does not touch.

Re-validation, the Release binary launched elevated through `.tools\launch-elevated.ps1` with
`/nosaveload`, the toolbar and geometry read back through the elevated broker against the live
window, strictly read-only (no scan, no recover, no file touched, no reboot):

- Geometry, config cleared: GetWindowRect 640 by 480, GetWindowPlacement showCmd 1 with a
  normal size of 640 by 480. Cascade position 104,104 from CW_USEDEFAULT, as the original.
- Toolbar empty area (ToolbarWindow32 child, point 609,13): reads 240,240,240, the light flat
  band.
- Previous-versions ListView present with 0 items, the expected empty state on a machine with
  no Volume Shadow Copies.
- The window closed cleanly on WM_CLOSE and no `PreviousFilesRecovery.cfg` was written next to
  the executable under `/nosaveload`.
