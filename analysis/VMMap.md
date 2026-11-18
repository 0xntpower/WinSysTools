# VMMap

Source-accurate C++20 and WTL recreation of the Sysinternals VMMap process
memory analyzer, snapshot path only. Original target `targets/sysinternals/vmmap64.exe`
(FileVersion 3.33, 748 KB, x64). Recreation in `src/VMMap/`.

## What it does

VMMap takes a snapshot of one process's virtual address space. A picker lists
running processes, then the main window shows a summary table of memory
categories over a detail list of every region. The recreation walks the address
space with `VirtualQueryEx`, reads per-page physical-memory attributes with
`QueryWorkingSetEx`, and classifies each region into the same categories the
original uses.

## Reverse engineering findings

Engine was the IDA idalib MCP (primary), cross-checked against the original's
own CSV dump. Resources were extracted with Resource Hacker.

### Manifest and window

- The embedded manifest (`resource 1 24`) requests **asInvoker**, not
  requireAdministrator. The description reads "Process handle and DLL viewer"
  and it depends on common-controls v6. The recreation matches asInvoker in
  `VMMap.manifest`, so same-user targets need no elevation. VMMap raises
  `SeDebugPrivilege` when available to reach more processes, which the
  recreation also does, best effort.
- Main window class is **VmMapClass**, built from the `MAIN_WINDOW` dialog
  template (376 x 355 dialog units, position 150, 150, `DS_CENTER`, resizable
  with `WS_THICKFRAME`, `WS_MAXIMIZEBOX`, `WS_MINIMIZEBOX`). Caption is
  "VMMap - Sysinternals: www.sysinternals.com". The recreation converts the
  376 x 355 dialog-unit footprint to pixels from the MS Shell Dlg 8pt base
  units and centers on the work area for the first launch, about 580 x 636 px
  at 96 DPI. Saved placement in the registry overrides on later launches.
- The summary pane is tree control id 1016 and the detail pane is tree control
  id 1003. The original draws its own multi-column trees. The recreation uses
  two report-mode list views, which carry the same columns.
- VMMap has **no toolbar band**. The bottom row holds four plain BUTTON
  controls (Timeline, Heap Allocations, Call Tree, Trace), so the toolbar
  light fix from the guide does not apply. `PinToLightMode()` keeps the menus,
  headers and list background light on a dark-mode system.

### EULA gate

`sub_140004128` composes `Software\Sysinternals\%s` with the per-tool name and
checks `EulaAccepted` (REG_DWORD) at, in order, `HKLM\Software\Sysinternals`,
`HKCU\Software\Sysinternals` and `HKCU\Software\Sysinternals\VMMap`
(`sub_140004098`). The `-accepteula` and `/accepteula` switches record consent
under the per-tool key. `Eula.h` mirrors this flow exactly.

### Columns

Two column-definition arrays live in `.rdata`, each record a string pointer, a
width and an alignment flag.

- Summary table (array at 0x14006c660): Type, Size, Committed, Private,
  Total WS, Private WS, Shareable WS, Shared WS, Locked WS, Blocks, Largest.
- Detail list (array at 0x14006c7d0): Address, Type, Size, Committed, Private,
  Total WS, Private WS, Shareable WS, Shared WS, Locked WS, Blocks, Protection,
  Details.

### Categories

`sub_14002A750` fills the summary tree, inserting the category rows in this
order: Image, Mapped File, Shareable, Heap, Managed Heap, Stack, Private Data,
Page Table, Unusable, Free, under a Total aggregate root. The category-name
pointers sit in a `.data` table around 0x1400801b0. `CategoryName` in
`MemSnapshot.h` reproduces the names exactly, including "Private Data" rather
than the short "Private" used in the column headers.

### Picker

The process picker is the `SELECT_OR_LAUNCH_DIALOG` tabbed dialog. Its select
tab lists columns Name, PID, User, Private Bytes, Working Set and Architecture
(table at 0x14006b698). The recreation reproduces the select tab as a single
process-select dialog with those columns. The second tab is the launch-and-trace
feature, out of scope.

## Snapshot model

- Walk with `VirtualQueryEx` from zero to the maximum application address, one
  detail row per returned region.
- Classification by `MEMORY_BASIC_INFORMATION`: `MEM_FREE` is Free, `MEM_IMAGE`
  is Image with the mapped file path, `MEM_MAPPED` is Mapped File with the path
  (or Shareable when no backing file), and `MEM_PRIVATE` is Stack, Heap or
  Private Data.
- Stacks come from each thread's TEB. `NtQueryInformationThread` gives the
  TEB base, and `NT_TIB.StackBase` at TEB offset 8 locates the stack
  reservation, whose allocation base marks the whole region as Stack.
- Heaps come from the PEB heap list. `NtQueryInformationProcess` gives the PEB
  base, and `NumberOfHeaps` at PEB offset 0xE8 with `ProcessHeaps` at 0xF0 gives
  the heap base array. Regions whose allocation base matches a heap base are
  Heap.
- Working-set columns come from `QueryWorkingSetEx` per committed page. Valid
  pages count toward Total WS, the Shared attribute bit splits Shareable WS from
  Private WS, a share count above one gives Shared WS, and the Locked bit gives
  Locked WS.
- Unusable is the granularity-locked part of each free block, computed from the
  64 KB allocation granularity. Free is the rest of the free block.
- The Total row sums the memory-type categories and excludes Free and Unusable.
- Header figures (Committed, Private Bytes, Working Set) come from
  `GetProcessMemoryInfo`.

## Out of scope

Reproduced in the menu and bottom buttons for fidelity, documented here as not
implemented, all part of the invasive launch-and-trace timeline feature that
injects a tracing DLL (the `TRACEDLL` resource and the Detours `.detourc` and
`.detourd` sections):

- Launch and trace, the trace snapshot interval and the timeline graph.
- Call Tree, Heap Allocations and the per-allocation stack traces.
- Fragmentation View, Strings and Configure Symbols.

## Build

VS 2022 Community MSBuild, PlatformToolset v143, stdcpp20, Unicode, static CRT,
x64 Debug and Release. Both configurations build clean. WTL headers from
`C:\WTL10_10320_Release\Include`. Not added to any solution, per request.

## Validation

A benign `notepad.exe` was spawned and both the original and the recreation
were pointed at the same PID with the documented `-p <pid> <outputfile>`
command line, which dumps a CSV and exits. The recreation implements that same
headless dump, sharing its exporter with the GUI Save As.

Both reports carry identical summary and detail column layouts. On the same
process the categories and columns line up, and several figures match to the
byte after the KB conversion, for example on one run:

- Free size 135,284,359,232 KB in both, Free blocks 86 in both.
- Image size 392,924 KB, committed 391,352 KB, blocks 981, all identical.
- Mapped File size 97,928 KB, committed 97,928 KB, blocks 50, all identical.
- Total size and committed agree within the normal snapshot-to-snapshot drift.

Region-level checks on stable regions:

- notepad.exe and ntdll.dll images appear at the same base addresses in both,
  as category Image, Execute/Read on the code sections, with identical file
  paths in the Details column.
- The Thread Environment Block and Process Environment Block are Private Data,
  Read or Read/Write, and labeled.
- Stacks are category Stack with Reserved and committed sub-regions.
- Heaps are category Heap, Read/Write, labeled Heap ID N.

The recreation displays values in kilobytes in the GUI and in the CSV, matching
the original. The GUI launch geometry is 580 x 636 px, matching the original's
376 x 355 dialog-unit footprint. The window background renders light on a
dark-mode system. Evidence in `analysis/VMMap-gui.png` (the main window over a
notepad snapshot) and `analysis/VMMap-about.png` (the About credit).

### MainFrame header and source split (2026-11-18)

`CMainFrame`'s window-class method bodies were moved out of `MainFrame.h` into a
new `MainFrame.cpp`, leaving the header with only the class shape: members, the
WTL macros, the message map and the method declarations. The move is behaviour
preserving and matches the WinObj split that came before it, followed by the HRC
clang-format pass over `src/VMMap`. `MemSnapshot.h` is a namespace of inline free
functions and data rather than a class, so it stayed a header and only took the
format pass. `MainFrame.h` went from 816 to 163 lines, the new `MainFrame.cpp`
is 744 lines, and the new unit was added to `VMMap.vcxproj`. Both x64
configurations rebuild clean.

The rebuilt Release binary was pointed at a `notepad.exe` spawned for the check.
The summary category table filled with all 11 category rows and the detail
region list filled with 607 region rows. The window came up at 580 x 636 px,
client 564 x 577, matching the first-launch footprint. No `ToolbarWindow32`
child exists, confirming VMMap has no toolbar band, so the toolbar light fix
stays inapplicable. Backgrounds read light under a `PrintWindow` capture: the
detail list at 255,255,255, the summary list at 240,240,240 and the menu bar at
255,255,255. The capture matches the pre-refactor window.

### Known differences

All consistent with a clean snapshot recreation and noted deliberately:

- The original splits a single allocation into a parent row and child
  sub-region rows in the detail list. The recreation shows one flat row per
  `VirtualQueryEx` region. Every region is present, without the parent grouping.
- The original marks images loaded at their ASLR address as "Image (ASLR)". The
  recreation shows "Image". The category is the same.
- Heap detection uses the PEB primary heap bases. Additional heap segments
  beyond a heap's base are not followed, so a small amount of heap shows as
  Private Data. The combined Heap and Private Data figures stay consistent.
- The Private column (committed private bytes) is counted for private regions.
  The copy-on-write private commit inside images is reported as zero, so the
  Total Private figure runs a little under the original. The Private WS column,
  which is read from the working-set attributes, matches.
- Managed Heap needs the .NET data-access layer and is not detected. A managed
  process's garbage-collected memory would show as Private Data. Page Table is
  kernel-side memory not visible through `VirtualQueryEx`. Both rows are present
  and read zero, as they do for a native process such as notepad.

## Security and safety

VMMap reads the memory map, which is addresses, sizes, types and protection,
never memory contents. Validation used only a benign notepad spawned for the
test. No view-contents path exists. The tool is pure user mode with no kernel
driver.
