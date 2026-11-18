# WinUpdatesView -- reverse-engineering notes

Target: `targets/winupdatesview-x64/WinUpdatesView.exe` -- NirSoft WinUpdatesView
v1.20, Windows Updates History Viewer. **x64 PE, Unicode, not packed.**
md5 `bf5b643087ac4b7381e06ab546b058f2`,
sha256 `de86e80b7d2b88ebaf04dfe1a84ac2a65fbcb62d0fc4577ce7730ea2492a6fda`.
Image base 0x140000000. 364 functions, 255 strings (IDA). Engines used -- IDA
idalib session `winupdatesview`, plus rea/Ghidra 12.1.2 snapshot
`analysis/snapshots/winupdatesview.snap`.

## Data sources (LoadFrom dispatcher `sub_140016248`, field at ctx+1104)
The tool reads the Windows Update history from one of four sources, chosen by the
`LoadFrom` setting (Advanced Options window, F9):

| LoadFrom | Source | Core function |
|----------|--------|---------------|
| 1 (default) | This computer, using the Windows Update Agent COM API | `sub_140015348` |
| 2 | This computer, read `DataStore.edb` directly | `sub_140016194` (ESE parser) |
| 3 | External database file | `sub_140015A5C` / `sub_140016194` |
| 4 | Remote computer `\\<name>\Admin$\...\DataStore.edb` | `sub_140016194` |

The default, and the path this reconstruction implements faithfully, is the WUA
COM API. The direct-database modes (2/3/4) walk the ESENT `DataStore.edb` file at
`Windows\SoftwareDistribution\DataStore\DataStore.edb` with a hand-written ESE
reader and can temporarily stop the Windows Update service when the file is
locked (`StopWinUpdatesService`). That ESE parser is large and cannot be
exercised on this machine (no external `.edb`, live DB locked), so it is
documented here but left unimplemented in the recreation (see Deviations).

## WUA COM core -- `sub_140015348` (confirmed with rea)
```
CoCreateInstance(CLSID_UpdateSearcher, NULL, CLSCTX_INPROC_SERVER|CLSCTX_LOCAL_SERVER,
                 IID_IUpdateSearcher, &searcher)
searcher->GetTotalHistoryCount(&count)            // vtbl +176
searcher->QueryHistory(0, count, &history)        // vtbl +144  -> IUpdateHistoryEntryCollection
history->get_Count(&n)                            // +72
for i in 0..n:
    history->get_Item(i, &entry)                  // +56  -> IUpdateHistoryEntry
    read fields, append record to the list model
```
GUIDs recovered from `.rdata` (raw bytes), matching `wuapi.h`:
- `rclsid` @0x140019618 = **CLSID_UpdateSearcher** `{B699E5E8-67FF-4177-88B0-3684A3388BFB}`
- `riid`   @0x140019990 = **IID_IUpdateSearcher** `{8F45ABF1-F9AE-4B95-A933-F0F66E5056EA}`
- QI iid   @0x1400199A0 = **IID_IUpdateHistoryEntry2** `{C2BFB780-4539-4132-AB8C-0A8772013AB6}`

### Fields read from each IUpdateHistoryEntry (exact method set)
| Method (vtbl off) | Type | Stored as |
|-------------------|------|-----------|
| get_Operation (+56) | UpdateOperation | Update Operation |
| get_ResultCode (+64) | OperationResultCode | Operation Result |
| get_HResult (+72) | LONG | hResult |
| get_Date (+80) | DATE | Install Date (UTC FILETIME via VariantTimeToSystemTime) |
| get_UpdateIdentity (+88) -> get_UpdateID (+64) / get_RevisionNumber (+56) | BSTR / LONG | Update ID / Revision Number |
| get_Title (+96) | BSTR | Title |
| get_Description (+104) | BSTR | Description |
| get_UnmappedResultCode (+112) | LONG | Unmapped Result Code |
| get_ClientApplicationID (+120) | BSTR | Client Application ID |
| get_ServerSelection (+128) | ServerSelection | Server Selection |
| get_ServiceID (+136) | BSTR | Service ID |
| get_UninstallationNotes (+152) | BSTR | Uninstall Notes |
| get_SupportUrl (+160) | BSTR | Support URL |
| QI IUpdateHistoryEntry2 -> get_Categories (+168) -> Item(0)->get_Name | BSTR | Category |

Information URL is derived, not a WUA field. The core scans the Title for the
first `KB` token, collects the digits after it, and formats
`http://support.microsoft.com/kb/<digits>`. The KB Number column is a separate
parse of the Title (`(KB....)` up to `)`, or `- KB....` up to a space).

## Columns -- display order and titles (string ids 1001..1017, verified by /stext)
The cell dispatcher `sub_140002534` selects the field and its formatting by
column index. The order below is confirmed byte-for-byte against the original's
`/stext` output. Widths are the saved `Columns` blob defaults.

| # | Title | width | source / formatting |
|---|-------|-------|---------------------|
| 0 | Title | 150 | Title string |
| 1 | Description | 150 | Description string |
| 2 | KB Number | 120 | parsed from Title |
| 3 | Install Date | 120 | FILETIME, GetDateFormat SHORTDATE + GetTimeFormat, local unless GMT |
| 4 | Update Operation | 120 | enum 1 Install, 2 Uninstall |
| 5 | Operation Result | 120 | enum 0 Not Started .. 5 Aborted |
| 6 | Information URL | 250 | KB support URL |
| 7 | Support URL | 250 | SupportUrl |
| 8 | Uninstall Notes | 250 | UninstallationNotes |
| 9 | Category | 200 | Categories[0].Name |
| 10 | Client Application ID | 150 | ClientApplicationID |
| 11 | Service ID | 150 | ServiceID |
| 12 | Update ID | 150 | UpdateIdentity.UpdateID |
| 13 | Revision Number | 110 | decimal |
| 14 | Unmapped Result Code | 110 | decimal |
| 15 | Server Selection | 120 | enum 0 Default, 1 Managed Server, 2 Windows Update, 3 Others |
| 16 | hResult | 120 | `0x%8.8x` |

Enum string tables (resource ids): Operation 901 Install / 902 Uninstall.
Result 910 Not Started, 911 In Progress, 912 Succeeded, 913 Succeeded With
Errors, 914 Failed, 915 Aborted. ServerSelection 920 Default, 921 Managed
Server, 922 Windows Update, 923 Others.

## Config -- `HKCU\Software\NirSoft\WinUpdatesView` (and optional `.cfg` INI via /cfg)
Value names recovered from the binary and its shipped `.cfg`: `ShowTimeInGMT`,
`HideItemsCompatibility`, `HideWindowsDefender`, `MarkOddEvenRows`,
`ShowGridLines`, `SaveFilterIndex`, `ShowInfoTip`, `LoadFrom`,
`StopWinUpdatesService`, `ExternalFilename`, `ComputerName`, `UseQuickFilter`,
`QuickFilterString`, `QuickFilterColumnsMode`, `QuickFilterFindMode`,
`QuickFilterShowHide`, `QuickFilterCaseSensitive`, `AddExportHeaderLine`,
`MainFont` (LOGFONT), `WinPos` (WINDOWPLACEMENT), `Columns` (width/index pairs),
`Sort`. The `Columns` blob is an array of (WORD width, WORD columnIndex) in
display order. `HideWindowsDefender` hides items whose Category is
`Microsoft Defender Antivirus` or `Windows Defender`.

## Command-line switches
Export (each takes a filename): `/stext /stab /scomma /shtml /sverhtml /sxml
/sjson`. Also `/cfg <file>`, `/sort <column|~column>` (repeatable, column index
or title), `/LoadFrom <1..4>`, `/ExternalFilename <file>`, `/ComputerName
<name>`, `/StopWinUpdatesService <0|1>`. `/savelangfile` is present in the
binary (language-file dump) and is not reconstructed.

## Export encodings and formats (recovered by running the original)
- `/stext`: UTF-16LE + BOM. Each record is wrapped in a 50-'=' rule and a blank
  line. Field names are left-justified to a minimum width of 18, then ": ".
- `/stab`: UTF-16LE + BOM. Optional header line. Every field (the last included)
  is followed by a tab.
- `/scomma`: **UTF-8 + BOM**. Optional header line. Comma-separated, no trailing
  comma, values quoted when they contain a comma, quote or line break.
- `/shtml`: UTF-16LE + BOM. NirSoft `mytable` template, per-column `th` widths,
  a `Created by using` credit line.
- `/sverhtml`: UTF-16LE + BOM. Vertical table per record.
- `/sxml`: UTF-16LE + BOM. `<?xml version="1.0" ?>`, root
  `windows_updates_history`, each `<item>` with slugged tag names.
- `/sjson`: UTF-16LE + BOM. Array of objects keyed by the full column titles.

## UI (resources)
Main menu 102, context menu 104, accelerators 103. Dialogs -- Properties 105,
Exception 107, Advanced Options 111, About 112, Quick Filter toolbar 114, Column
Settings 1096. Toolbar bitmap 110 (seven 16x16 images). Status bar shows
`%d item(s)` + `, %d Selected` and the Windows version. Commands 40001..40052
(Save 40001, Exit 40002, About 40003, Refresh 40004, Grid Lines 40005, HTML
reports 40006/40010, Select All 40007, Copy 40008, Properties 40009, Choose
Columns 40011, Auto Size 40012/40024, Find 40014, Deselect 40015, Advanced
40030, Show Time GMT 40042, Hide Defender 40047, Save All 40048, Copy Cell
40051, Add Header Line 40052).

## Reconstruction
Clean C++20 + WTL under `src/WinUpdatesView/`, x64 Unicode, output
`WinUpdatesView.exe`. Files: `WinUpdatesView.cpp` (WinMain + CLI),
`UpdateRecord.h` (record + per-column formatting), `UpdateEnumerator.cpp/.h`
(WUA COM reader), `Columns.h`, `Export.cpp/.h` (seven formats),
`Config.h` (registry + `.cfg`), `RegKey.h`, `MainFrame.h`, `Dialogs.h`,
`resource.h`, `res/WinUpdatesView.rc`. Links ole32, oleaut32, comctl32,
shell32, advapi32, comdlg32, gdi32, version. CLSID and IID are declared from
`wuapi.h`, which carries the exact recovered GUIDs, so no `wuguid.lib` is needed.

## Deviations
- LoadFrom 2/3/4 (direct ESENT `DataStore.edb` reading, local, external and
  remote) are documented but not implemented. The recreation shows an
  information message and keeps the API path. The API path is the default and
  the validated core.
- The recreation uses a stable sort. The original uses `qsort`, which is not
  stable, so records that share an identical sort key (for example two history
  entries with the same Title) can appear in a different relative order. Field
  values are identical.
- MainFont selection, Quick Filter toolbar, Run As Administrator and
  `/savelangfile` are not reconstructed.

## Status -- DONE, validated
- Debug and Release both build for **x64**.
- `/stext` diff against the original: **1519 of 1519 records byte-identical as a
  set** (same file size 2,836,446 bytes, every field matches). The only textual
  difference is the relative order of records whose Title is identical, caused
  by the original's unstable `qsort` versus the recreation's stable sort. A
  record-multiset comparison is identical.
- `/scomma`, `/stab`, `/sxml`, `/sjson`, `/shtml` encodings and structures match
  the original (CSV UTF-8, the rest UTF-16LE), with the same benign tie-order
  delta.
- GUI launches, loads 1519 items via the WUA API, shows the correct columns in
  order, status bar `1519 item(s), 1 Selected` and the Windows version, no
  crash. Screenshot at `analysis/winupdatesview-gui.png`.

## Fidelity and HRC pass (2026-11-17)

**Config storage -- FIXED (location).** The original persists settings in
`WinUpdatesView.cfg` next to the executable, section `[General]`, not in the registry.
Confirmed in the path builder `sub_140010D38` and the value table `sub_1400021DC`: the
binary imports only `GetPrivateProfile*`/`WritePrivateProfile*` and no registry API at
all. The recreation had stored config under `HKCU\Software\NirSoft\WinUpdatesView`. It
now writes the `.cfg` INI with the exact original format -- decimal integers, strings
direct, and `WinPos` and `Columns` as uppercase space-separated hex bytes (`%2.2X `),
with `Columns` as `(WORD width, WORD columnIndex)` pairs. `/cfg` repoints the store for
both the window and the headless export. Value names and defaults match `sub_1400021DC`.

**HRC v1.1 light touch.** The config was rewritten in HRC style -- `nullptr`,
`static_cast` over C-style casts, every local initialized, `[[nodiscard]]` on the shared
readers, no new unscoped enum. The format lives in the shared helper
`src/common/CfgIni.h`. The registry wrapper include was dropped from the config header.

**Rebuild and re-validation.** Debug and Release x64 both build clean. Config
round-trips to `WinUpdatesView.cfg`: seeded ShowGridLines, MarkOddEvenRows and
ShowTimeInGMT are preserved, `WinPos` is written as `2C 00 00 00 ...` hex, and no
registry key is created. The `/scomma` export still runs and produces the correct
header and column order (unchanged code, 716498 bytes on this machine).
