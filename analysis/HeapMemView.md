# HeapMemView -- reverse-engineering notes

Target: `targets/heapmemview-x64/HeapMemView.exe` -- NirSoft HeapMemView v1.05, x64
PE, **ANSI/MultiByte internally** (every import is the `...A` variant), not packed.
Image base 0x140000000. md5 `650bd2ff9615e35c884e5948b8d14e2c`. 275 functions, 309
strings. Build string in the binary --
`f:\Projects\VS2005\HeapMemView\x64\Release\HeapMemView.pdb`. Engines used -- IDA
idalib session `heapmemview` (primary), rea/Ghidra snapshot
`analysis/snapshots/heapmemview.snap` (cross-check).

HeapMemView shows the allocated heap memory blocks of a process the user selects. The
upper pane lists one row per busy block (data address, header address, block size, heap
handle, segment index, a new marker and a short data preview). The lower pane shows the
selected block as a hex dump.

## Entry / startup
- `start` @0x14000ee60 -- VC-style CRT (`__set_app_type`, `_initterm`, `__getmainargs`).
- `WinMain` @0x14000c614 refuses platforms earlier than Windows 2000 (`dwPlatformId == 2`
  and `dwMajorVersion >= 5`, else the message box "This utility cannot work on operating
  system earlier than Windows 2000"). It enables SeDebugPrivilege and sets
  `SetErrorMode(0x8001)`.
- The GUI builds WNDCLASS "HeapMemView" (`sub_14000C398`, WndProc `sub_1400017AC`), menu
  102, accelerators 103, icon 101. `sub_1400090AC` is the create handler.

## Command line (all three recovered, the only switches in this version)
- `/nosaveload` -- do not read or write the configuration file.
- `/savelangfile` -- write the translation template `HeapMemView_lng.ini` and quit.
- `/deleteregkey` -- `RegDeleteKey HKCU\Software\NirSoft\HeapMemView` and quit. That
  registry root is the legacy store, the live configuration is an INI file.

This build has no `/s*` export or action switches. Those were added to other NirSoft
tools later.

## Heap enumeration (the core)
The driver `sub_140002074` builds the block list for a process:

1. `OpenProcess(PROCESS_ALL_ACCESS, pid)` for the read handle.
2. `CreateToolhelp32Snapshot(TH32CS_SNAPHEAPLIST, pid)`.
3. `Heap32ListFirst` / `Heap32ListNext` walk the HEAPLIST32 entries. Each
   `th32HeapID` is a heap base address.
4. For every heap it picks a walker by OS major version (`sub_140004130` reads
   GetVersionEx once). Major version 5 or lower uses the pre-Vista walker
   `sub_140004E00`, Vista and later use the NT6+ walker `sub_140004C68`.

The tool does **not** call `Heap32First` / `Heap32Next` (they are not imported). Those
return only the busy blocks and hide the header addresses and the encoded sizes.
Instead each walker reads the live heap structures directly with `ReadProcessMemory`.

### NT6+ walker (`sub_140004C68`), recovered exactly
Per heap, starting from the heap base as the first `_HEAP_SEGMENT`:
- read 0x1B0 bytes of the `_HEAP` / `_HEAP_SEGMENT` header.
- segment signature check -- the dword at offset 0x10 equals `0xFFEEFFEE`.
- `FirstEntry` at offset 0x40, `LastValidEntry` at offset 0x48.
- the heap Encoding key -- the size key is the word at offset 0x88, the flags key is the
  byte at offset 0x8A (the `_HEAP.Encoding` entry lives at offset 0x80).
- walk entries from FirstEntry while below LastValidEntry. Each `_HEAP_ENTRY` is 16 bytes
  and on x64 the first eight bytes are unused, so the Size word sits at entry offset 8
  and the Flags byte at entry offset 0x0A, both XOR-encoded with the Encoding key.
- block size is `16 * (sizeWord ^ encSizeKey)`, in 16-byte units. flags is
  `flagsByte ^ encFlagsKey`. A read failure skips one page, a zero size ends the segment.
- the next segment is reached through the list link at offset 0x18, used only when
  `(link & 0xFFF) == 0x10`, where the segment base is `link - 0x10`. At most 64 segments.

### Pre-Vista walker (`sub_140004E00`)
Reads 0xAC8 bytes of the `_HEAP`, takes a 64-slot segment pointer array at offset 0x80,
reads each `_HEAP_SEGMENT` (0x68 bytes, same `0xFFEEFFEE` signature at 0x10, FirstEntry
at 0x38, LastValidEntry at 0x60), and walks 16-byte entries whose size word at offset 0
is not encoded. This path does not run on the validation OS and is kept for fidelity
with the original version split.

### Block record and filter (`sub_140001F64`)
Each walker reports {heap, header address, raw size, flags byte, segment index}. The
collector keeps a block only when the busy bit (`flags & 1`) is set and the data size
(raw size minus the 16-byte header) passes the size filter. A block that is new since the
last refresh is marked for the New column and the pink highlight.

## Record field map and columns
The stored record holds heap, header, raw size, flags and segment index. The getter
`sub_140001E24` maps the display column index to a field, and the compare `sub_140001DCC`
gives the sort key. Titles come from STRINGTABLE 1001..1007. The default order is:

| idx | column         | value                         | format  |
|-----|----------------|-------------------------------|---------|
| 0   | Data Address   | header + 16                   | 0x%016x |
| 1   | Header Address | header (the _HEAP_ENTRY)       | 0x%016x |
| 2   | Block Size     | raw size - 16 (data bytes)     | decimal |
| 3   | Heap Handle    | th32HeapID / heap base         | 0x%016x |
| 4   | Segment Index  | segment ordinal                | decimal |
| 5   | New            | "*" when newly appeared         |         |
| 6   | Data Preview   | first N data bytes, ASCII/hex   |         |

Every x64 heap block has a 16-byte `_HEAP_ENTRY` header, so the data address is the
header plus 16 and the displayed block size is the raw size minus 16. Sort is numeric by
header address for the address columns, by raw size for Block Size, by ordinal for
Segment Index.

## Data preview and lower pane
`sub_140002728` reads `DataPreviewBytes` bytes (capped at 256) from the data address plus
`DataPreviewStartByte` and formats them as ASCII or `"%2.2X "` hex per `DataPreviewMode`.
Control bytes and, unless `DisplayAboveAscii127` is on, bytes at or above 0x80 render as a
dot. The lower pane is a read-only Courier New edit (`sub_1400090AC` creates it) that
shows the selected block as a hex dump, `BytesPerLine` bytes per line, with an optional
per-line address when `DisplayMemoryAddress` is on and the header included when
`DisplayMemHeader` is on.

## Config -- HeapMemView.cfg (INI, next to the exe), section `[General]`
Value names and defaults recovered from `sub_14000B114` and `sub_140001B90`:

    ShowGridLines 0, SaveFilterIndex 0, VerSplitLoc 0x3FFF, BytesPerLine 16,
    DisplayMemHeader 0, NewMemoryBlocksMarkColor 0xE0FFFF, MarkNewMemoryBlocks 1,
    Filter.FromSize 80, Filter.ToSize 10000, Filter.UseFromSize 1, Filter.UseToSize 0,
    DefaultExportExt "bin", DisplayAboveAscii127 0, DataPreviewBytes 32,
    DataPreviewMode 0, DataPreviewStartByte 0, SelectProcessSort 0, LastProcess "",
    DisplayMemoryAddress 1, AutoSortOnRefresh 0, Sort 0

Plus `WinPos` (a WINDOWPLACEMENT written as hex bytes) and `Columns` (a blob of
little-endian width and column-index pairs, four bytes per visible column). The language
file `HeapMemView_lng.ini` carries `[general]` and `[strings]` sections and is read by
`sub_140005C4C` and `sub_14000510C`.

## Process selection (`sub_14000D318`)
The Select Process dialog (template 109, columns Process ID, Process Name, Process Path
from STRINGTABLE 1101..1103) resolves processes through psapi `EnumProcesses` plus
`GetModuleFileNameEx` and `GetModuleInformation`, with a Toolhelp `Process32First` /
`Process32Next` fallback, all loaded by `GetProcAddress`.

## Toolbar / menus / dialogs
`CreateToolbarEx` loads bitmap 104 (8 images, 16x16) with nine entries. The buttons, by
bitmap index and command, are Select Process (6, 40023), Save (1, 40001), Filter
(7, 40032), Refresh (5, 40004), Copy (2, 40008), Properties (3, 40009), Find (0, 40014)
and Exit (4, 40002). Main menu 102, context menu 104, accelerators 103. Dialogs --
Properties 105, Exception 107, Select Process 109, Filter 111, About 112, Advanced
Options 113, Column Settings 1096.

## Export (list) and memory export
Save Selected Items offers the formats in STRINGTABLE 501..507 -- Text, Tab delimited,
Tabular, HTML Horizontal, HTML Vertical, XML and Comma delimited. Report title "Memory
Blocks List" (string 8), footer "Created by using" (string 6), save title "Select a
filename to save" (string 7). Export Memory Data To File (40027, Ctrl+E) dumps the raw
bytes of selected blocks to `.bin` files ("Memory Binary File", `DefaultExportExt`),
a single file for one block or a folder for several.

## Reconstruction
Clean C++20 plus WTL under `src/HeapMemView/`, x64 Unicode, built to `HeapMemView.exe`.
- `HeapBlock.h` -- the record, the column getter and the sort compare.
- `HeapEnumerator.cpp` / `.h` -- the heap list walk and both structure walkers, with the
  busy and size filter.
- `ProcessList.cpp` / `.h` -- process enumeration for the Select Process dialog.
- `Columns.h` -- the seven columns in the recovered default order.
- `Config.h` -- HeapMemView.cfg with every value name and default.
- `Export.cpp` / `.h` -- the seven list formats plus HTML.
- `HexDump.h` -- the Data Preview and the lower pane hex formatting.
- `Dialogs.h` -- Select Process, Filter, Advanced Options, Column Settings, Properties
  and About over the extracted templates.
- `MainFrame.h` -- the WTL frame with the toolbar, the two-part status bar, the upper
  owner-data ListView, the lower Courier New edit and the draggable divider, the options,
  refresh and find.
- `HeapMemView.cpp` -- WinMain and the three command-line switches.
- `res/HeapMemView.rc` -- the extracted resources (includes prepended, letter
  accelerators quoted, FW_NORMAL and FALSE in the FONT statements replaced with 400 and 0
  which rc.exe rejects under RC_INVOKED).

## Cross-check (rea / Ghidra)
The rea snapshot `analysis/snapshots/heapmemview.snap` confirms the heap-list driver and
the two walkers over Ghidra. The import set shows `Heap32ListFirst` and `Heap32ListNext`
but no `Heap32First` or `Heap32Next`, which is the evidence that the block walk reads the
heap structures directly rather than through the Toolhelp block API.

## Validation
Both Debug and Release build for x64. Validated against the live `explorer.exe`
(PID 3260, 58 heaps). A small console probe over the reconstructed `EnumerateHeapBlocks`
returned 2157 busy blocks across 55 heaps with data addresses sitting 16 bytes past each
header, which proves the 2013-era NT heap offsets still match the Windows 11 layout.

The original and the rebuild were then run against the same process through the GUI. Both
show the same seven columns in the same order, both report about 2175 Memory Blocks, and
the first rows match block for block -- header 0x1090740 size 80, 0x10907a0 size 80,
0x1090890 size 4096, 0x10918a0 size 80, 0x1091900 size 352, 0x1091a70 size 464, all under
heap handle 0x1090000 at segment 0. Live heap contents shift between runs, so the exact
block count drifts by a few blocks and the volatile rows differ, which is why the heap
list and the column mapping are compared rather than the full row set. The toolbar renders
its icons rather than a dark box. Screenshot of the top strip at
`analysis/HeapMemView-gui.png`.

## Deviations
- The recreation is a Unicode build using the W APIs, where the original is ANSI. This
  does not affect the heap walk or the numeric columns.
- Startup opens an empty window like the original and waits for the user to pick a process
  with Select Process. The original posts a custom message on create, the recreation does
  not reproduce that internal message, only the observed behavior.
- The pre-Vista walker is written to mirror the decompiled offsets but is not exercised on
  the validation OS, which always takes the NT6+ path.

## Fidelity and HRC pass (2026-11-17)

**Config storage -- FORMAT FIXED.** The store location was already correct: the
original keeps settings in `HeapMemView.cfg` next to the executable, section
`[General]` (ANSI), with the legacy `HKCU\Software\NirSoft\HeapMemView` only cleared by
`/deleteregkey`. Confirmed again in `sub_14000B114`. The blob hex format was wrong,
though: the recreation wrote lowercase packed hex (`%02x`), while the original writes
uppercase bytes separated by one space (`%2.2X `), confirmed in the blob writer
`sub_14000DC88` through `sub_1400044D8` and the paired reader that skips spaces. The
recreation `hexBytes` and `parseHexBytes` were changed to the uppercase space-separated
format and to skip spaces when reading, so `WinPos` and `Columns` are byte-compatible
with the original `.cfg`. Location, section and value names were already correct and
left as is.

**HRC v1.1 light touch.** The `itoa` buffer in the INI writer is now initialized at
declaration, and the new parse and format helpers use `nullptr` and `static_cast`. This
ANSI tool keeps its own inline helpers rather than the Unicode `CfgIni.h`.

**Rebuild and re-validation.** Debug and Release x64 both build clean. Config
round-trips to `HeapMemView.cfg`: seeded ShowGridLines, BytesPerLine and
DataPreviewBytes are preserved and `WinPos` is written as `2C 00 00 00 ...` hex.

## MainFrame.cpp split (2026-11-18)

Moved every `CMainFrame` window-class method body out of `MainFrame.h` into a new
`MainFrame.cpp`, matching the WinObj golden shape. The header now carries only the class
declaration -- members, the static constexpr `kDivider` and `kSplitRange`, the member
initializers, `DECLARE_FRAME_WND_CLASS`, the message map, and the method declarations.
The bodies moved in source order, unchanged. `override` and `static` stay on the
declarations and come off the out-of-line definitions, `const` stays on both, and the WTL
handler signatures keep their trailing `BOOL&`. `MainFrame.cpp` was added to the project
ClCompile list. The `OnEraseBkgnd` to `FillFrameBtnFace` hand-off and the
`PinToolbarLight` plus `PinToLightMode` calls are byte-for-byte the same.

- **MainFrame.h**: 736 lines before, 179 after (declarations only).
- **MainFrame.cpp**: new, 929 lines (about 27 KB).
- **clang-format**: the HRC `--style=file` pass ran over both files, clean.
- **Build**: Debug and Release x64 both rebuilt clean under the VS 2022 Community MSBuild.

**Live validation.** Spawned a plain Notepad, launched the rebuilt Release build with
`/nosaveload`, and drove it from PowerShell:
- Window class reads `HeapMemView`, geometry is `(0,0)` at `640x480`, matching the
  original `CreateWindowExA` rect.
- The toolbar child -- found with `FindWindowExW(frame, 0, "ToolbarWindow32", null)` --
  reads `240,240,240` in its empty band, so the dark-toolbar fix still holds on this
  dark-mode host.
- Pointing it at the self-spawned Notepad (its row matched by PID in Select Process)
  filled the upper list with 91 heap blocks and set the caption to
  `HeapMemView - Notepad.exe [PID]`. Only the block count was read back, never any heap
  content of the inspected process.
