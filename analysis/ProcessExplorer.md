# Process Explorer (procexp) recreation

Source-accurate C++20 plus WTL recreation of the Sysinternals Process Explorer, built as a focused core subset. The flagship is very large, so the work reproduces the live process tree, the per-process data engine, the DLL and handle lower panes, a per-process properties sheet, the colour scheme, the menu, the toolbar and Find, and stubs the rest for fidelity.

Original analysed: `targets/sysinternals/procexp64.exe`, version 16.0, July 2023 build, x64.

## Reverse engineering findings

Engines used: IDA idalib MCP as the primary decompiler (session opened and closed cleanly), plus string and resource extraction. The resource set and manifest came out of Resource Hacker and mt.exe, and the runtime facts came from IDA and the wide-string dump. The findings cross-checked across all three sources.

### Identity and manifest
- Embedded manifest (resource 1), matched byte-for-fact by the rebuild:
  - `name="Process Explorer"`, `version="16.0.0.0"`, `type="win32"`
  - `description` of `Process handle and DLL viewer`
  - dependency on `Microsoft.Windows.Common-Controls` `6.0.0.0` (the v6 themed controls, so the dark-toolbar fix applies)
  - `requestedExecutionLevel level="asInvoker" uiAccess="false"`. Process Explorer runs as the invoker and gains full detail only when the user chooses Show Details for All Processes, which relaunches it elevated. No requireAdministrator.
  - `dpiAware` true
  - a `compatibility` block listing the five supportedOS GUIDs (Windows 7 through Windows 10)
- Window class name: `PROCEXPLORER` (registered through a generic window factory at `sub_14006B190`, with the class string at `0x1401312C8`).
- Window title: `Process Explorer - Sysinternals: www.sysinternals.com`, with the current user context appended as `[COMPUTER\user]`.

### EULA and settings
- EULA value name `EulaAccepted` (REG_DWORD), checked under `HKLM\Software\Sysinternals`, then `HKCU\Software\Sysinternals`, then `HKCU\Software\Sysinternals\Process Explorer`. The `-accepteula` and `/accepteula` switches record consent under the per-tool key without prompting. This mirrors the shared Sysinternals gate.
- Settings key: `Software\Sysinternals\Process Explorer`.
- Colour setting value names confirmed in the binary: `ColorOwn`, `ColorServices`, `ColorPacked`, `ColorImmersive`, `ColorRelocatedDlls`, `ColorJobs`, `ColorDelProc`, `ColorNewProc`, `ColorNet`, `ColorProtected`, `ColorSuspend` (each with a `Dark` variant), plus `HighlightDuration` and `HighlightOwnProcesses`. The default RGB values are applied at runtime by the settings reader rather than stored statically, so the colour variables at `0x14018E3D8` read as zero in the image. The recreation reproduces the default light scheme and honours the registry values when present.

### Geometry
The main window default rect global at `0x14016C468` holds `{0x80000000, 0x80000000, 0, 0}`, which is CW_USEDEFAULT for position and the system default for size. So the original lets the window manager place and size the window on first run, then saves and restores its WindowPlacement. The recreation does the same by creating with the WTL default rect and restoring a saved placement.

### Resources
The real main menu (resource 101), its accelerators (resource 101), the About box layout, and the application icon (resource 101) were taken from the extracted resources. The menu is reproduced in full so every command id matches the original, with out-of-scope items stubbed or greyed. The embedded 37 KB `150 BINRES` blob is the tool's helper PE and is not needed, since no kernel driver is written or loaded.

## Core reproduced

- Process tree as an indented report list. Processes nest under their parent by parent PID plus a creation-time check that rejects a reused parent id, with orphans and the system processes rooted at the top. Each row carries the process icon and the columns Process, PID, CPU, Private Bytes, Working Set, Description and Company Name. A one second timer refreshes it.
- Data engine over `NtQuerySystemInformation(SystemProcessInformation)`, one call giving every process with its CPU times, memory counters, handle and thread counts, and the threads inline. CPU percent is the kernel plus user time delta over the refresh interval against wall time times the processor count. Private Bytes comes from PrivatePageCount and Working Set from WorkingSetSize. The image path comes from QueryFullProcessImageName, the Description and Company Name from the image version info, the icon from the image, and the owning user from the process token. Metadata is cached by process identity so each refresh only pays for new processes.
- Lower pane with a View menu toggle between DLLs and Handles. DLLs enumerate through Toolhelp Module32 with Name, Description, Company Name and Path. Handles enumerate through `NtQuerySystemInformation(SystemExtendedHandleInformation)` filtered to the PID, each duplicated into the viewer so NtQueryObject resolves the Type and Name.
- Per-process Properties sheet with an Image tab (path, command line, current directory, user, start time, parent, PID, description, company, version), a Threads tab (TID, CPU, start address) and a TCP/IP tab (connections via GetExtendedTcpTable for IPv4 and IPv6).
- Row colour scheme in the light default: own processes light blue, service-hosting processes pink, suspended processes gray, newly created processes flash green and exited processes flash red for the highlight duration. Service processes are found by mapping active service PIDs through EnumServicesStatusEx. Suspended is derived from every thread being in the suspended wait state.
- The full menu, a light toolbar built from an icon image list with the shared PinToolbarLight plus WM_ERASEBKGND fix, a status bar showing the process count and processor count, update-speed selection with a Paused state, column-ordered Save to a tab-separated file, and Find Handle or DLL which searches every process's handles and modules for a substring.
- Kill, Suspend and Set Priority are wired for fidelity. Kill asks for confirmation. These were validated by inspection and never exercised against a real process.

## Validation

Launched the rebuild and the original side by side on the same machine.

- Toolbar: the `ToolbarWindow32` empty area reads RGB 240,240,240 across five sample points. The band is light on a dark-mode system, matching the other tools.
- Geometry: first run with the config cleared uses CW_USEDEFAULT, matching the original default rect. Saved WindowPlacement restores on later runs.
- Tree and data against the original for a stable set of processes: parent and child nesting matches (System Idle to System to smss and Memory Compression, wininit to services to the svchost group), and the PID, Description and Company Name match exactly for Registry 276, smss 836, Memory Compression 3344, csrss 1304, wininit 1396, services 1480, svchost 1712, WmiPrvSE 4560 and 5128, CrossDeviceService 14144, SearchHost 15104 and WidgetService 6452. Private Bytes and Working Set match within refresh drift. CPU tracks sensibly, with the System Idle Process high and ordinary processes low.
- Row colours match the scheme: service-hosting svchost rows render pink and the user's own processes render light blue, as in the original's light theme.
- Lower pane on a benign process launched for the test. For cmd.exe the DLL view lists the nine loaded modules with Description, Company Name and Path (cmd.exe, ntdll.dll, KERNEL32.DLL, KERNELBASE.dll, ucrtbase.dll, Wldp.dll, msvcp_win.dll, combase.dll). The Handle view lists 83 handles with Type and Name resolved, including the `\KnownDlls` Directory, the Image File Execution Options Key, and Event, TpWorkerFactory, IoCompletion and WaitCompletionPacket entries.
- Properties for cmd.exe: the Image tab shows the path `C:\Windows\System32\cmd.exe`, the exact command line, the current directory, the user `NORT\itag7`, the parent PID, the start time, and the version 10.0.26100.1. The Threads tab lists the thread ids.
- About credit renders exactly: `Independent source-accurate recreation of Process Explorer by Sysinternals, through reverse engineering and rewritten in modern C++20 & WTL 10.` and `Author: 0xntpower`, with a Sysinternals link.
- Build: Debug and Release, x64, both clean with VS 2022 Community MSBuild and the v143 toolset.

### MainFrame header and source split

The `CMainFrame` window-class method bodies were moved out of `MainFrame.h` into a new `MainFrame.cpp`, matching the WinObj layout. The header keeps the class shape alone: the members, the `DECLARE_FRAME_WND_CLASS` macro, the message map, the static constants and the method declarations. The move is behaviour-preserving, with the HRC clang-format pass applied to both files. `MainFrame.h` went from 1121 to 224 lines, and `MainFrame.cpp` is 1135 lines. The new source was added to the project under ClCompile.

Rebuilt and revalidated the Release x64 binary after the split:
- Both Debug and Release x64 rebuild clean.
- The toolbar `ToolbarWindow32` empty area reads RGB 240,240,240 across five sample points.
- The process tree fills with the seven columns (Process, PID, CPU, Private Bytes, Working Set, Description, Company Name) over the full live row set.
- Row colours render by the scheme, measured with real pixels: an `svchost.exe` service row reads RGB 255,225,225 pink and the viewer's own `ProcessExplorer.exe` row reads RGB 204,232,255 blue.
- The lower pane shows the DLL list for a benign `cmd.exe` launched for the check, nine modules, found by matching its PID in the tree.
- Window geometry restores from the saved WindowPlacement as before, unchanged by the split.

### Notes and known limits
- Thread start addresses come back as zero from `SystemProcessInformation` for a non-elevated viewer, because the modern kernel withholds that field. Full start-address and stack resolution needs elevation plus symbols, which is out of scope. The Threads tab still shows the TID and CPU.
- Handles and DLLs of a higher-integrity or AppContainer process are not readable without elevation, so those panes come up empty for such a target, matching the original without Show Details for All Processes. The Windows 11 Store Notepad runs in an AppContainer, so the validation used a classic Win32 process launched for the test.
- The upper pane is an indented report list that shows the tree fully expanded, which is the original's default state. Per-node expand and collapse with connecting tree lines is not reproduced.
- The colour defaults follow the original's light scheme by appearance. The exact default RGBs are set at runtime by the original rather than stored in the image, so they were not byte-extracted. The registry values are honoured when present.
- The dynamic Users menu that the original adds for multiple sessions is not reproduced.

## Out of scope (stubbed for fidelity or omitted, no kernel driver)

Reproduced as menu items, columns or tabs for fidelity where it was cheap, with the body left empty or omitted:
- VirusTotal integration and its column
- the System Information graphs window (CPU, memory, I/O, GPU)
- the toolbar CPU-history mini-graph and the tray icons
- the extra property tabs: Performance, Performance Graph, GPU, Disk and Network, .NET tabs, Environment, Strings, Security, Job
- kernel-driver-backed data such as thread stacks with symbols and deeper handle detail, since no kernel driver is written or loaded
- Replace Task Manager, which writes the Image File Execution Options registry
- Run as Limited User and Run at different privilege
- configure symbols
- save and column-set persistence beyond the basics (window placement, update speed, lower-pane mode and font are saved)
- Column Heatmaps, the Difference Highlight Duration dialog body, and the Theme switch (the recreation is pinned to light mode by design)

## Files

- `src/ProcessExplorer/ProcessExplorer.cpp` entry point, EULA gate, command line
- `src/ProcessExplorer/NtApi.h` the ntdll surface and system-information structures
- `src/ProcessExplorer/ProcessData.h` the live process data engine
- `src/ProcessExplorer/Modules.h` the DLL list for one process
- `src/ProcessExplorer/Handles.h` the handle list for one process
- `src/ProcessExplorer/MainFrame.h` the main window, tree-list, lower pane, toolbar and colours
- `src/ProcessExplorer/Dialogs.h` About, the property sheet and Find Handle or DLL
- `src/ProcessExplorer/Eula.h` the Sysinternals license gate
- `src/ProcessExplorer/res/ProcessExplorer.rc` menu, accelerators, icon and dialogs
- `src/ProcessExplorer/ProcessExplorer.manifest` matched to the original
