# TaskSchedulerView -- reverse-engineering notes

Target: `targets/taskschedulerview-x64/TaskSchedulerView.exe` -- NirSoft
TaskSchedulerView v1.74, a viewer for the Windows Task Scheduler. **x64 PE,
Unicode, not packed.** md5 `316988279672be6ca66268b793eb5308`, sha256
`43a522a7394be9cefe241b35fe9e338e65024357abf69abb306012f39e02b2c6`. Image base
0x140000000. 375 functions, 269 ASCII strings in the IDA cache (the UI strings
are UTF-16 and live in `.rdata` and `.rsrc`). Engines used -- IDA idalib session
`taskschedulerview`, plus rea/Ghidra snapshot
`analysis/snapshots/taskschedulerview.snap`. The PDB path left in the binary is
`f:\Projects\VS2005\TaskSchedulerView\x64\Release\TaskSchedulerView.pdb`.

## Data source -- Task Scheduler 2.0 COM API (LoadFrom = 1, the default)
The default and reconstructed path uses the Task Scheduler 2.0 COM API. The
connect-and-walk core is `sub_140003908` (confirmed with rea), which does

```
CoCreateInstance(&rclsid, NULL, CLSCTX_INPROC_SERVER, &riid, &service)
service->Connect(server, user, domain, password)     // vtbl +88
service->GetFolder(L"\\", &rootFolder)                // vtbl +56
```

GUIDs recovered from `.rdata` raw bytes, matching `taskschd.h`:
- `rclsid` @0x14001b668 = **CLSID_TaskScheduler** `{0f87369f-a4e5-4cfc-bd3e-73e6154572dd}`
- `riid`   @0x14001b678 = **IID_ITaskService** `{2faba4c7-4da9-4013-9697-20cc3fd40f85}`

The root folder is walked recursively. Each folder is read with
`GetTasks(TASK_ENUM_HIDDEN)` for its `IRegisteredTask` items and `GetFolders` for
its subfolders. The `LoadFrom = 2` (remote computer) path reuses the same COM
calls against a `Connect` to the named machine, and adds an `OpenSCManager` plus
`RegConnectRegistry` to start the Remote Registry service and read COM handler
details over the network. The original can also read task files under
`\\<name>\admin$\system32\Tasks` for a remote machine.

## Fields read per task
Scalar fields come from `IRegisteredTask` and its `ITaskDefinition`. The derived
columns come from the trigger and action collections, the account SIDs, the
registry CLSID entries, and the task file.

| Source | Methods | Columns |
|--------|---------|---------|
| IRegisteredTask | Name, Path, State, LastRunTime, NextRunTime, LastTaskResult, NumberOfMissedRuns, GetInstances | Task Name, Status, Last Run, Next Run, Last Task Result, Missed Runs, Running Instances |
| IRegistrationInfo | Author, Description, Source | Author, Description, Source |
| ITaskSettings | Hidden, AllowDemandStart, RunOnlyIfNetworkAvailable, WakeToRun, MultipleInstances, Priority | Hidden, Allow Demand Start, Run Only If Network Available, Wake To Run, Multi-Instances Policy, Priority |
| IPrincipal | UserId or GroupId, RunLevel | Running User Account, Highest Privileges Run |
| ITriggerCollection | Type per trigger plus the per-type fields | Triggers, Run On Boot, Run On Logon, Run On Event, Run Daily, Run Weekly, Run Monthly |
| IActionCollection | Type, Exec Path/Arguments/WorkingDirectory, ComHandler ClassId | Action Types, Executable File, Executable Arguments, Start Directory, COM Handler Class |
| Registry HKCR\CLSID\{clsid} | (Default) and InprocServer32\(Default) | COM Handler Description, COM Handler Filename |
| Task file | owner SID, created and modified timestamps, full path | Task Owner, Task File Created Time, Task File Modified Time, Task Filename |

Formatting rules recovered by running the original and comparing exports:
- **Status** uses the `TASK_STATE` enum, string ids 600 Unknown, 601 Disabled, 602 Queued, 603 Ready, 604 Running.
- **Last Task Result** is the raw result code printed as an unsigned decimal, so `0x80070002` shows as 2147942402.
- **Last Run** and **Next Run** come from the COM DATE values, which are already local time, formatted with GetDateFormat short date plus GetTimeFormat.
- **Triggers** lists the distinct trigger type names, sorted by trigger type value ascending. Six daily triggers collapse to one "Daily", and "Weekly, Idle, Logon" is the ascending order of types 3, 6 and 9. Names are string ids 700 Event, 701 Specific Time, 702 Daily, 703 Weekly, 704 Monthly, 705 Monthly Day-Of-Week, 706 Idle, 707 Task Registration, 708 Boot, 709 Logon, 711 Session State Change.
- **Run On Boot** and **Run On Logon** are Yes or No from the presence of a boot or logon trigger. **Run On Event** is the inner query of the event trigger subscription XML (what the `<Select>` element contains). **Run Daily** is "Every %d day(s)" and **Run Weekly** is "Every %d week(s) - On %s" with the day names from the DaysOfWeek bitmask (670 Sunday through 676 Saturday). Each is No when that trigger type is absent.
- **Action Types** lists the distinct action type names, string ids 680 Run EXE, 681 COM Handler, 682 Send Email, 683 Show Message.
- **Executable File** is the exec action Path with environment variables expanded. **Executable Arguments** and **Start Directory** are left raw.
- **COM Handler Filename** is the CLSID InprocServer32 value (expanded, with a bare dll name resolved against the system directory). **COM Handler Description** is the CLSID default value. **COM Handler Class** is the ClassId GUID.
- **Multi-Instances Policy** uses `TASK_INSTANCES_POLICY`, string ids 660 Parallel, 661 Queue, 662 Ignore New, 663 Stop Existing.
- **Running User Account** resolves the principal SID to the account name only. **Task Owner** resolves the task file owner SID to the domain and name form.
- **Task File Created Time** and **Task File Modified Time** are the task file timestamps shown in UTC, with no local conversion. **Task Filename** is `%windir%\system32\Tasks` joined with the task path.

## Columns -- display order and titles (string ids 1001..1037)
All 37 columns are visible by default. The display order below is confirmed
byte-for-byte against the original's `/scomma` header line. The column id is the
string-table id, and the internal index is that id minus 1001.

1 Task Name, 2 Status, 3 Hidden, 4 Last Task Result, 5 Last Run, 6 Next Run,
31 Allow Demand Start, 24 Triggers, 14 Run On Boot, 15 Run On Logon, 23 Run On
Event, 16 Run Daily, 17 Run Weekly, 18 Run Monthly, 32 Run Only If Network
Available, 34 Wake To Run, 33 Multi-Instances Policy, 7 Missed Runs, 8 Task
Folder, 22 Action Types, 9 Executable File, 10 Executable Arguments, 30 Start
Directory, 19 COM Handler Filename, 20 COM Handler Description, 21 COM Handler
Class, 28 Running User Account, 29 Highest Privileges Run, 11 Author, 12
Description, 13 Source, 25 Task Owner, 26 Priority, 27 Running Instances, 35 Task
File Created Time, 36 Task File Modified Time, 37 Task Filename.

## Config -- `HKCU\Software\NirSoft\TaskSchedulerView` (and optional `.cfg` INI via /cfg)
Value names recovered from the binary: `MarkOddEvenRows`, `ShowGridLines`,
`SaveFilterIndex`, `ShowInfoTip`, `ShowTimeInGMT`, `BlackBackground`,
`HideDisabledTasks`, `LoadFrom`, `ComputerName`, `AutoStartRemoteRegistry`,
`HideMicrosoftTasks`, `HideHiddenTasks`, `ShowOnlyRunningTasks`,
`AddExportHeaderLine`, `AlwaysOnTop`, `AutoRefresh`, `AutoSizeColumnsOnLoad`,
`MainFont`, `SaveFileEncoding`, `AlignNumbersToRight`, `UseQuickFilter`,
`QuickFilterString`, `QuickFilterColumnsMode`, `QuickFilterFindMode`,
`QuickFilterShowHide`, `QuickFilterCaseSensitive`, `TrayIcon`, `WinPos`,
`PropertiesPos`, `Sort`, `Columns`. The `Columns` blob is an array of (WORD
width, WORD columnIndex) in display order. `HideMicrosoftTasks` hides tasks whose
folder starts with `\Microsoft`.

## Command-line switches
Export, each taking a filename: `/stext /stab /stabular /shtml /sverhtml /sxml
/scomma /sjson`. Also `/cfg <file>`, `/nosaveload`, `/sort <column|~column>`
(repeatable, column index or title), `/nosort`, `/Columns <comma list>`,
`/LoadFrom <1|2>`, `/ComputerName <name>`, and the task actions `/Disable
/Enable /StopTask /RunNow <task>`. Present in the binary and not reconstructed --
`/savelangfile` (language-file dump), `/RunAsAdmin` (relaunch elevated), and
`/deleteregkey` (the recreation deletes the settings key for this one).

## Export encodings and formats (recovered by running the original)
- `/scomma` -- UTF-8 with a BOM. Optional header line. Comma-separated, no trailing comma, values quoted when they contain a comma, quote or line break.
- `/stext` -- UTF-16LE with a BOM. Each record is wrapped in a 50-'=' rule and a blank line. Field names are left-justified to a minimum width of 18, then ": ".
- `/stab` -- UTF-16LE with a BOM. Optional header line. Tab-separated with no trailing tab (the v1.74 fix for the extra empty field).
- `/sxml` -- UTF-16LE with a BOM. `<?xml version="1.0" encoding="UTF-16" ?>`, root `tasks_list`, each `<item>` with slugged tag names.
- `/shtml` and `/sverhtml` -- UTF-16LE with a BOM. NirSoft `mytable` template with a "Created by using" credit line to nirsoft.net.
- `/sjson` -- UTF-16LE with a BOM. Array of objects keyed by the full column titles.
- `/stabular` -- UTF-16LE with a BOM, fixed-width columns.
The `SaveFileEncoding` option forces ANSI, UTF-16 or UTF-8 for every format.

## UI (resources)
Main menu 102, context menu 104, tray menu 116, accelerators 103. Dialogs --
Properties 105 (a two-column property and value list), Exception 107, Advanced
Options 111 (This computer or Remote computer plus the Remote Registry option),
About 112, Quick Filter 114, Column Settings 1096. Toolbar bitmap 110 is a 144x16
24-bit strip of nine 16x16 images, built with CreateToolbarEx in `sub_14000F2A8`.
The eleven buttons are Enable (image 6), Disable (image 7), a separator, Save
(image 1), Refresh (image 5), Copy (image 2), Properties (image 3), Find (image
0), Sort By (image 8, a dropdown), and Exit (image 4). The status bar shows the
item count plus the selected count.

## Reconstruction
Clean C++20 plus WTL under `src/TaskSchedulerView/`, x64 Unicode, output
`TaskSchedulerView.exe`. Files -- `TaskSchedulerView.cpp` (WinMain and the CLI),
`TaskRecord.h` (the 37 precomputed cells plus sort keys), `TaskEnumerator.cpp/.h`
(the Task Scheduler 2.0 COM walk, the registry and SID helpers, and the
enable/disable/run/stop/xml operations), `Columns.h`, `Export.cpp/.h` (eight
formats), `Config.h`, `RegKey.h`, `MainFrame.h`, `Dialogs.h`, `resource.h`,
`res/TaskSchedulerView.rc`. COM uses ATL CComPtr for clean RAII. Links ole32,
oleaut32, comctl32, shell32, advapi32, comdlg32, gdi32, version and taskschd. The
toolbar is built with `LoadToolbarImageList(IDB_TOOLBAR)` from the shared
`common/UiUtil.h`, which keys the strip's black corner transparent, so the icons
render correctly rather than as a dark box.

## Build environment note
ATL ships only with the v145 toolset (14.51.36231) on this machine. The
v143-compatible toolset (14.44.35207) that the other projects name has no
`atlmfc` folder, so this project sets PlatformToolset to v145. The error from VC
itself recommends v145 for this toolset version. The other projects in the tree
would need the same bump or an ATL-for-v143 install to compile here.

## Deviations
- The direct task-file reading paths (local database and the remote `admin$` share) are documented but not implemented. The recreation uses the COM API for the local machine and a remote `Connect` for `LoadFrom = 2`, which is the default and validated core.
- A task with several Exec actions that share one Path shows the first action's Arguments. The original selects a different action for one such task (MareBackup), so that one Executable Arguments cell differs.
- The original emits an empty Task Filename for one task (OneDrive Per-Machine Standalone Update Task). The recreation emits the real path for it.
- Quick Filter, custom font selection, the tray icon, Auto Refresh and Always On Top are carried in the settings but only partially wired in the window. `/savelangfile` and `/RunAsAdmin` are not reconstructed.
- The recreation uses a stable sort. The original uses `qsort`, which is not stable, so records that share a sort key can appear in a different relative order while their field values match.

## Status -- DONE, validated
- Debug and Release both build for **x64**.
- `/scomma` back-to-back against the original -- the task set is **identical, 223 of 223 by Task Name and Task Folder**. Across the 37 columns the only stable-field difference is the single MareBackup Executable Arguments cell and the single OneDrive Task Filename cell noted above. The Last Run, Next Run, Missed Runs, Status and Running Instances fields drift between the two runs because tasks run and recompute next-run times, which is expected. The file created and modified times match once shown in UTC like the original.
- GUI launches, loads 223 tasks through the COM API, shows the columns in the correct order, and the toolbar shows colour icons rather than a black box. Screenshot at `analysis/TaskSchedulerView-gui.png`.

## Fidelity and HRC pass (2026-11-17)

**Config storage -- FIXED (location).** The original persists settings in
`TaskSchedulerView.cfg` next to the executable, section `[General]`, not in the registry.
Confirmed in the path builder `sub_1400119C8` and the value table `sub_1400020FC`: the
binary imports the `GetPrivateProfile*`/`WritePrivateProfile*` APIs for config and has no
`RegSetValueEx` or `RegCreateKeyEx`. Its `RegOpenKeyExW`/`RegQueryValueExW` are the
read-only COM-handler CLSID lookups. The recreation had stored config under
`HKCU\Software\NirSoft\TaskSchedulerView`. It now writes the `.cfg` INI with the exact
original format -- decimal integers, strings direct, and `WinPos` and `Columns` as
uppercase space-separated hex bytes (`%2.2X `), with `Columns` as `(WORD width, WORD
columnIndex)` pairs. `/cfg` repoints the store, and `/deleteregkey` still clears the
legacy registry root. Value names and defaults match `sub_1400020FC`.

**HRC v1.1 light touch.** The config was rewritten in HRC style -- `nullptr`,
`static_cast` over C-style casts, every local initialized, `[[nodiscard]]` on the shared
readers, no new unscoped enum. The format lives in the shared helper
`src/common/CfgIni.h`. The pre-existing unscoped `ColKind` was left as is because scoping
it would ripple across the 37 column initializers and the numeric-alignment check.

**Rebuild and re-validation.** Debug and Release x64 both build clean (v143 toolset as
configured). Config round-trips to `TaskSchedulerView.cfg`: seeded ShowGridLines,
MarkOddEvenRows and HideMicrosoftTasks are preserved, `WinPos` is written as
`2C 00 00 00 ...` hex, and no registry key is created. The `/scomma` export still runs
and produces the documented 37-column order (Task Name, Status, Hidden, Last Task
Result, Last Run, Next Run, ...).
