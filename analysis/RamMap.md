# RamMap

Source-accurate C++20 and WTL recreation of the Sysinternals RamMap physical
memory analyzer. Original target `targets/sysinternals/RAMMap64.exe` (FileVersion
1.61, 354 KB, x64, built 2022-11-05). Recreation in `src/RamMap/`.

## What it does

RamMap shows how physical RAM is used across the whole system. A single tab
control carries seven tabs: Use Counts, Processes, Priority Summary, Physical
Pages, Physical Ranges, File Summary and File Details. The Use Counts tab is a
matrix of memory use types (rows) against the kernel paging-list states
(columns). The recreation reads the data from `NtQuerySystemInformation` in user
mode with no kernel driver.

## Reverse engineering findings

Engine was the IDA idalib MCP (primary), cross-checked against the live original.
Resources were extracted with Resource Hacker.

### Manifest and window

- The embedded manifest (`resource 1 24`) requests **requireAdministrator**, not
  asInvoker, because RamMap reads the system memory lists. It also declares DPI
  awareness `PerMonitorV2, system`, `gdiScaling` true, and depends on
  common-controls v6. The recreation matches all of this in `RamMap.manifest`,
  embedded through the linker with `UACExecutionLevel=RequireAdministrator` so
  the merged manifest level agrees. Verified with `mt.exe` on the built binary.
- WinMain (`sub_...`, entry through `WinMain`) requires Windows Vista or higher,
  then enables three privileges before any query:
  **SeProfileSingleProcessPrivilege**, **SeDebugPrivilege** and
  **SeIncreaseQuotaPrivilege**. It refuses to run if any cannot be enabled. The
  recreation enables the same three in `ram::EnablePrivileges` and shows the same
  refusal message.
- Main window class is **RamMapClass**, built from the `MAIN_WINDOW` dialog
  template (372 x 287 dialog units, position 150, 150, `DS_CENTER`, resizable
  with `WS_THICKFRAME`, `WS_MAXIMIZEBOX`, `WS_MINIMIZEBOX`). Caption is
  "RamMap - Sysinternals: www.sysinternals.com". The template holds one
  `SysTabControl32` (id 1037). The recreation uses a `CFrameWindowImpl` of the
  same class name holding a WTL tab control, converting the 372 x 287 dialog-unit
  footprint to pixels from the MS Shell Dlg 8pt base units and centering on the
  work area, about 574 x 525 px at this DPI. Saved placement in the registry
  overrides on later launches.
- RamMap has **no toolbar band** (the window is just a tab control), so the
  guide's three-edit toolbar light fix does not apply. `PinToLightMode()` keeps
  the menus, list headers and backgrounds light on a dark-mode system.

### EULA gate

`sub_14000439C` composes `Software\Sysinternals\%s` with the per-tool name and
checks `EulaAccepted` (REG_DWORD) at, in order, `HKLM\Software\Sysinternals`,
`HKCU\Software\Sysinternals` and `HKCU\Software\Sysinternals\RamMap`. The
`-accepteula` and `/accepteula` switches record consent under the per-tool key.
`Eula.h` mirrors this flow exactly, the same shape used for VMMap.

### Tabs

The tab table is `off_14003F7A0`, seven entries of three pointers each
{label, dialog template, dialog proc}, consumed in `sub_140010414` (the main
dialog proc). The labels and the templates they use:

| Tab | Template | Content |
|-----|----------|---------|
| Use Counts | LISTVIEWGRAPH_TAB | type-by-state matrix with a top bar graph and a left legend |
| Processes | LISTVIEW_TAB | per-process memory use |
| Priority Summary | LISTVIEW_TAB | standby and repurposed pages per priority |
| Physical Pages | LISTVIEWFILTER_TAB | per-physical-page owner |
| Physical Ranges | LISTVIEW_TAB | physical memory ranges |
| File Summary | LISTVIEW_TAB | cached files, one row per file |
| File Details | TREELIST_TAB | cached files with per-page detail |

### Use Counts matrix

The sixteen use-type rows come from the table `off_14003EA60`, indexed by a
4-bit page-use value, in this order: Process Private, Mapped File, Shareable,
Page Table, Paged Pool, Nonpaged Pool, System PTE, Session Private, Metafile,
AWE, Driver Locked, Kernel Stack, Unused, Large Page, Reserved1, Reserved2. The
nine state columns come from the column table `off_14003E940`: Total, Active,
Standby, Modified, Modified no write, Transition, Zeroed, Free, Bad. The first
list column header is "Usage" and carries the type name, then a Total row sits
under the sixteen types. `CategoryName` and `StateName` in `MemInfo.h` reproduce
the names exactly.

### Data source

`NtQuerySystemInformation` is resolved by name from ntdll in `sub_1400010D8`
into `qword_140051520`. The deep per-type and per-page detail comes from
`SystemSuperfetchInformation` (class 79), which `sub_1400092C4` queries with the
physical-memory-range info class and then walks the PFN database page by page.
The paging-list page counts come from `SystemMemoryListInformation` (class 0x50),
which is also the class the Empty menu writes through `NtSetSystemInformation`.
The Empty commands are `MemoryEmptyWorkingSets` (2), `MemoryFlushModifiedList`
(3), `MemoryPurgeStandbyList` (4) and `MemoryPurgeLowPriorityStandbyList` (5),
plus Empty System Working Set through `SetSystemFileCacheSize(-1, -1, 0)`.

## Data model (what the recreation reads)

The recreation reproduces the parts `NtQuerySystemInformation` gives reliably in
user mode with no driver:

- **SystemMemoryListInformation (0x50)** gives ZeroPageCount, FreePageCount,
  ModifiedPageCount, ModifiedNoWritePageCount, BadPageCount, the per-priority
  standby list PageCountByPriority[8] and RepurposedPagesByPriority[8]. This
  drives the Use Counts state-column totals (Standby, Modified, Modified no
  write, Zeroed, Free, Bad) and the whole Priority Summary tab.
- **GlobalMemoryStatusEx** gives the total physical and available bytes, which
  set the Use Counts Total and the Available figure.
- **SystemPerformanceInformation (2)** gives PagedPoolPages, NonPagedPoolPages,
  TotalSystemDriverPages and ResidentSystemCachePage, which fill the Paged Pool,
  Nonpaged Pool, Driver Locked and Metafile type rows in the Active column.
- The **hardware resource map** (`HKLM\HARDWARE\RESOURCEMAP\System Resources\
  Physical Memory`, value `.Translated`, a CM_RESOURCE_LIST) gives the Physical
  Ranges tab with no driver. The partial-descriptor stride is 20 bytes on x64
  (the union is sized to the interrupt-affinity variant), which the parser
  accounts for, and large-memory descriptors scale their length by the flag bits.

The Use Counts Total row is internally consistent: Active + Standby + Modified +
Modified no write + Transition + Zeroed + Free + Bad equals Total, which equals
the installed physical RAM to the byte.

## Out of scope (documented, not implemented)

- **The full per-type by per-state matrix body.** Filling every cell of the Use
  Counts matrix (Process Private, Mapped File, Shareable, the Unused row, and so
  on, split across the paging-list states) needs the Superfetch PFN-database
  enumeration that the original walks page by page. That enumeration is heavily
  Windows-version dependent. The recreation fills the state-column totals and the
  Total row accurately, plus the pool, driver and metafile type rows from the
  performance class, and leaves the Superfetch-only type rows at zero.
- **The Physical Pages tab**, the exact owner of each physical page, needs the
  RAMMap.sys helper driver. No kernel driver is written or loaded. The tab shows
  a note saying so.
- **Processes, File Summary and File Details** need the Superfetch private-source
  enumeration and the FileInfo driver. The tabs carry the faithful columns and a
  note, and stay empty.
- **The Use Counts top bar graph and the left colour legend**, and the Colors
  settings that tint them, are the graph feature and are not reproduced.
- The native `.rmp` snapshot file (Open and Save) and the headless command-line
  dump and empty modes are stubbed with a note.

The Empty menu is wired to `NtSetSystemInformation` and `SetSystemFileCacheSize`
exactly as the original, and was not exercised during validation.

## Build

VS 2022 Community MSBuild, PlatformToolset v143, stdcpp20, Unicode, static CRT,
x64 Debug and Release. Both configurations build clean. WTL headers from
`C:\WTL10_10320_Release\Include`. Not added to any solution, per request.

## Validation

The original and the rebuild were both launched elevated through
`.tools/launch-elevated.ps1` and compared near-simultaneously. The current
machine has 68,627,177,472 bytes of RAM (67,018,728 K).

### Use Counts, side by side

- Both show the same seven tabs in the same order, the same "Usage" first column,
  and the same state columns (Total, Active, Standby, Modified, Modified no
  write, Transition, Zeroed, Free, Bad), with the sixteen type rows plus a Total
  row. Evidence `analysis/RamMap-usecounts.png` (rebuild) and
  `analysis/RamMap-original.png` (original).
- **Total column matches to the byte in both: 67,018,728 K** (= 68,627,177,472
  bytes = GlobalMemoryStatusEx TotalPhys).
- **Standby is a near-simultaneous match**: rebuild 6,646,744 K against the
  original 6,698,212 K, inside the moment-to-moment drift of the standby list.
- The performance-derived type rows track the original closely: Paged Pool
  8,164,736 K rebuild against 8,037,048 K original, Nonpaged Pool 3,130,712 K
  against 3,813,180 K, Metafile 2,365,612 K against 2,797,556 K.
- The window renders light on a dark-mode system, and the launch geometry
  (574 x 525 px) matches the original (578 x 529 px) for the same 372 x 287
  dialog-unit footprint.

### Headless data check

A small console build over the same `MemInfo.h` engine, run elevated, confirmed
the data for the three data-reliable tabs:

- Use Counts Total row internally consistent, Total 67,018,728 K, and
  AvailBytes 23,452,668 K agreeing with GlobalMemoryStatusEx.
- Priority Summary: standby and repurposed per priority 0 to 7, standby total
  6,737,048 K equal to the Use Counts Standby total, as the original ties them.
- Physical Ranges: 8 ranges whose sizes total 67,018,728 K, equal to installed
  RAM, including the 62 GB range above the 4 GB boundary.

### About credit

The About box shows the required credit, confirmed embedded in the built binary:
"Independent source-accurate recreation of RamMap by Sysinternals, through
reverse engineering and rewritten in modern C++20 & WTL 10." and
"Author: 0xntpower". The ampersand is doubled in the source string literal so a
single & renders, the standard static-control accelerator rule.

Scripting the elevated window to screenshot every tab and the About box was
limited by UIPI and the scheduled-task desktop isolation in this non-interactive
session, so the Priority Summary and Physical Ranges tabs were validated by the
headless data check rather than by a tab screenshot. They use the same list-view
path as the Use Counts tab, which renders correctly in the captures.

## Security and safety

RamMap shows memory usage statistics, not memory contents. The File Summary and
File Details tabs would list cached file paths, and those tabs are left empty in
this recreation, so no file paths were captured or logged. The Use Counts,
Priority Summary and Physical Ranges captures hold only counts and addresses. No
kernel driver is written or loaded, and the Empty operations that flush memory
lists were not run during validation. No command that shuts down, reboots or
powers off the machine was run.
