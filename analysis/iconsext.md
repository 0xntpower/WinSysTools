# IconsExtract — reverse-engineering notes

Target: `targets/iconsext/iconsext.unpacked.exe` — NirSoft IconsExtract v1.47, x86 PE,
**ANSI/MultiByte**, VC6-era. The shipped `iconsext.exe` is UPX-packed, this
`.unpacked.exe` is the already-unpacked copy. Image base 0x400000.
sha256 of the unpacked copy `17e1dbe1e41b4d2adb6a8a056245e6cae61271484de3da1c7d934eaa80b5472a`.
Engines used — IDA idalib session `iconsext`, cross-checked with rea to Ghidra
12.1.2 snapshot `analysis/snapshots/iconsext.snap`.

Domain — scans files and folders for the icons and cursors stored in PE files
(EXE/DLL/OCX/CPL and others), lists them with live previews, extracts selected
icons to `.ico` (cursors to `.cur`), and copies a single icon to the clipboard.

## Entry / startup (WinMain @0x40626D)
- `sub_401F39({8, 0xFF})` is InitCommonControlsEx. On fail, MessageBox
  "Error: Cannot load the common control classes." (`@0x40b248`), return 1.
- `GetVersionExA`, `sub_407EA6` early init.
- `sub_4040DC(lpCmdLine)` parses the command line into a 264-byte-per-slot argv
  array at `0x40BF60` (slot 0 = `String1`), `0x40C068` (slot 1), `0x40C170` (slot 2), ...
- If `String1` == `/savelangfile` → `sub_40408A` writes the translation template, exit.
- `sub_403FB5` loads the language file (`iconsext_lng.ini`, keys charset /
  TranslatorName / TranslatorURL / rtl).
- If the command line is non-trivial and `sub_4060D5()` succeeds → exit 0 (the
  `/save` command-line extract path).
- GUI — `RegisterClassA` class "IconsExtract" (`sub_40607E`),
  `CreateWindowExA` 640x480 WS_OVERLAPPEDWINDOW, `LoadAcceleratorsA(0x67)`,
  `PostMessage(WM_USER+4 = 0x404)` to kick the startup search, message loop with
  `TranslateAcceleratorA`.

## Command-line switches (strings dumped with the IDA string cache)
- `/save <source file> <save folder> [-icons] [-cursors] [-asico]` (`sub_4060D5`)
  extract and quit. Neither `-icons` nor `-cursors` means nothing is written.
  `-asico` writes cursors as `.ico` instead of `.cur`.
- `-scanpath <path|wildcard>` pre-fills the search path (`sub_4051F2` tail). A bare
  directory gets `\*.*` appended (`asc_40B1B4`).
- `/savelangfile` writes `iconsext_lng.ini`.
- `/noloadsettings` skips loading `iconsext.cfg` on startup (`sub_4051F2`).

## Core data model — the found-icon record (296 bytes, stride 296)
Built by the `EnumResourceNames` callback `sub_404A01`, formatted per column by
`sub_40423B`.

| off  | field | source |
|------|-------|--------|
| +0   | numeric resource id | `sub_4017E7(name)` — INTRESOURCE value or `atoi` of a `#NNN` name, else 0 |
| +4   | image count | `sub_401532` = GRPICONDIR `idCount` |
| +8   | source file path | current file (`lParam+728`, set in `sub_404C2B`) |
| +272 | 16x16 image-list index | `ImageList_ReplaceIcon` |
| +276 | type | 1 Icon, 2 Cursor (RT_GROUP_ICON 14 → 1, RT_GROUP_CURSOR 12 → 2) |
| +280 | selected flag | set to 1 per item in the `/save` loop |
| +284 | resource name | INTRESOURCE id or the string name used by FindResource |
| +292 | total size | `sub_4014A4` = sum of every entry's `dwBytesInRes` |

## Columns — main list (sub_4051F2 InsertColumn, values sub_40423B)
Titles come from string ids 1101..1106. Widths match the shipped `iconsext.cfg`
`Columns` blob (pairs of width + column index) exactly.

| # | Title (strid) | width | value |
|---|---------------|-------|-------|
| 0 | Resource (1101) | 150 | `basename(id)` or `basename(name)`, e.g. `shell32.dll(5)` (`sub_4013E4`) |
| 1 | Images (1102) | 70 | image count, decimal |
| 2 | Library (1103) | 120 | file basename (`sub_4024DB`) |
| 3 | Type (1104) | 60 | "Icon" / "Cursor" (`sub_401399`, strings 2 / 1) |
| 4 | Total Size (1106) | 100 | total bytes, thousands-grouped (`sub_402C7E`) |
| 5 | Filename (1105) | 300 | full file path (record+8) |

View modes (menu / `sub_405B20`) — Large Icons (40004, LVS_ICON), Small Icons
(40005), List (40015), Details (40016). Toolbar bitmap 104 holds 5 images, and the
6 buttons are Search(40011) Save(40001) Copy(40022) Properties(40009) sep Exit(40002).

## Scan engine
- File / wildcard walk `sub_404D64` — split the input into directory + pattern
  (`sub_402495`), `FindFirstFile` over the pattern, and for each non-directory file
  call `sub_404C2B`. With the SubDir flag set, `FindFirstFile(dir\*.*)` recurses
  into each subdirectory with `dir\sub\pattern`.
- Per-file enum `sub_404C2B` — `LoadLibraryExA(file, LOAD_LIBRARY_AS_DATAFILE=2)`,
  then `EnumResourceNamesA(RT_GROUP_ICON)` if the icons flag is set and
  `EnumResourceNamesA(RT_GROUP_CURSOR)` if the cursors flag is set, callback
  `sub_404A01`, then `FreeLibrary`.
- Size / colour filter `sub_401610` — walks the GRPICONDIR entries and accepts the
  group when one entry matches the requested width, height and bit-count (0 = any).
- Preview icons `sub_401599` + `sub_40178A` — `LookupIconIdFromDirectoryEx(grp,
  fIcon=TRUE, cx, cy, 0)` picks the member id, then
  `CreateIconFromResourceEx(member, size, fIcon = type==1, 0x30000, cx, cy, 0)`.
- Process scan mode (ScanMode 2) enumerates a target process's loaded modules and
  scans each as a file.

## The icon-save core (sub_401807 / sub_405779 / sub_405646)
The core of the tool and the part cross-checked with rea. `sub_401807` rebuilds an
ICONDIR from the module's RT_GROUP_ICON / RT_GROUP_CURSOR directory and appends each
RT_ICON / RT_CURSOR member image.

Parameters — `hFile`, `hModule`, `resName`, `a4` = resource kind (1 icon, 2 cursor),
`a5` = output ICONDIR type (1 `.ico`, 2 `.cur`). Saving a cursor as `.ico` passes
a4 = 2, a5 = 1.

1. `FindResource` the group directory. `idCount` is at offset 4.
2. Allocate the ICONDIR. Header = reserved(0), type(a5), count. Entries follow.
3. For each GRPICONDIRENTRY (14 bytes) copy the first 12 bytes into the 16-byte
   ICONDIRENTRY (through `dwBytesInRes`). For cursors, take width from the low byte,
   height from the low byte of the second word halved, zero colorCount and reserved,
   subtract 4 from `dwBytesInRes` (the stripped hotspot), and when writing a `.cur`
   read the member's first two words as the hotspot into the planes / bitCount
   fields.
4. `imageOffset` is the running total plus the header size. The running total adds
   each entry's `dwBytesInRes`, and for cursors subtracts a further 4 (the original's
   accumulator quirk, reproduced for byte fidelity).
5. Write the 6-byte header and the entry array, then append each member image. For
   cursors the 4-byte hotspot is stripped from the written image data.

**rea / Ghidra confirms `sub_401807` line-for-line** — same group-directory walk,
same 12-byte entry copy, same cursor width/height/hotspot handling, same member
loop with the `size - 4` cursor strip.

Save loop `sub_405779` — for each selected record, load the source library (cached
by filename), build the output name (`sub_405646`) and call the file writer
(`sub_401A0F` → `CreateFile` + `sub_401807`). Output type is `.ico` unless the record
is a cursor and the "save cursors as ico" flag is clear, then it is `.cur`.

Output filename `sub_405646` — basename with everything from the first `.` dropped
(`sub_402815` replaces `.` with NUL), then `<base>_<id><ext>`, colliding as
`<base>_<n>_<id><ext>`. For shell32.dll the files are `shell32_<id>.ico`.

## Config — iconsext.cfg (INI next to the exe)
v1.42 moved the configuration out of the registry into `iconsext.cfg`. The value
names are taken verbatim from the shipped file. Section `[General]` —
`WinPos` (WINDOWPLACEMENT bytes), `Columns` (width + index pairs), `LVMode`,
`Sort1`, `Save.Folder`, `Save.CursorsAsico`, `Search.Wildcard`, `Search.FindColors`,
`Search.FindWidthHeight`, `Search.SearchCursors`, `Search.SearchIcons`,
`Search.SubDir`, `Search.FindColorsValue`, `Search.FindHeightValue`,
`Search.FindWidthValue`, `Search.ScanMode`. There is no
`HKCU\Software\NirSoft\IconsExtract` registry key in this version.

## Menus / dialogs (resources 102 / 104 / 106, 105 / 107 / 108 / 112)
- Menu 102 — File (Search For Icons Ctrl+I 40011, Save Selected Icons Ctrl+S 40001,
  File Properties F8 40025, Properties Alt+Enter 40009, Exit 40002), Edit (Copy
  Selected Icon Ctrl+C 40022, Select All Ctrl+A 40007), View (Large/Small/List/
  Details 40004/40005/40015/40016), Help (About 40003). Context menu 104, in-search
  "Stop" menu 106.
- Dialog 105 Search For Icons, 107 Save Selected Icons, 108 Properties, 112 About.
- Accelerators 103 — Ctrl+A/C/I/S, F8, Alt+Enter.

## Deviations from the original (all noted deliberately)
- Unicode (WTL) rather than the original ANSI build. All recovered data is ASCII and
  resource names are ANSI, so the saved `.ico`/`.cur` bytes are identical.
- The scan runs synchronously with a wait cursor rather than the original's
  message-pumped scan with a live "Stop" menu. ponytail — no background thread, which
  is adequate for the single-file and folder scans the tool is used for.
- `/savelangfile` writes a minimal seed template rather than the full original UI
  string dump. The switch is recognised and the file is created.
- The Properties per-image preview pane is a simple static-icon control rather than a
  custom-painted surface. The image list, dimensions, colours and sizes are full.

## Status
Data, presentation and the save core fully recovered and reconstructed as clean
C++20 + WTL under `src/iconsext/`, reusing the extracted resources
(`res/iconsext.rc`). Debug and Release (Win32) both build.

Validation:
- **Icon count vs the original** — `/save "C:\Windows\System32\shell32.dll" <folder>
  -icons` run with both the original `iconsext.unpacked.exe` and the rebuild. Both
  produce **335** `.ico` files, and all **335 are byte-identical** (SHA-256). The GUI
  shows **340 item(s)** for shell32.dll, which is 335 icons + 5 cursors. The rebuild
  and the original agree on 335 icons and 5 cursors.
- **Cursor path** — `/save ... -cursors` on comctl32.dll gives 18 `.cur` files,
  **18/18 byte-identical**, and `-cursors -asico` gives 18 `.ico` files,
  **18/18 byte-identical**. shell32.dll's 5 cursors are also 5/5 byte-identical. The
  cursor hotspot handling and the accumulator quirk are reproduced exactly.
- **.ico validity** — a saved file has a valid ICONDIR (reserved 0, type 1, count 8),
  opens through `System.Drawing.Icon`, and loads through Win32 `LoadImage`
  (LR_LOADFROMFILE) to a non-null handle.
- **GUI** — the rebuild launches without crashing, shows the Search For Icons dialog
  pre-filled from `-scanpath`, scans shell32.dll, and displays the icons in Large
  Icons view with the `shell32.dll(N)` resource labels, the File/Edit/View/Help menu,
  the 6-button toolbar from bitmap 104, and the "340 item(s)" status bar. Screenshot
  `analysis/iconsext-gui.png`. Both Debug and Release built (Win32).

## Fidelity and HRC pass

Config storage (already correct). A string scan of the original finds `.cfg`, the
`General` section, and the per-key names `Save.Folder`, `Search.Wildcard`, `WinPos`, and
the rest, and finds no `HKCU\Software\NirSoft\IconsExtract` key. This is the
`iconsext.cfg` store the original moved to in v1.42. The recreation already derives
`iconsext.cfg` from the module name and uses the same section and value names, so config
storage was left unchanged.

HRC v1.1 light touch. Added `[[nodiscard]]` to `WriteIconToFile`, `SaveIconToPath`,
`BuildSavePath`, and `ExtractGroupIcon`, where a dropped result is a lost error or a
leaked HICON. `SaveEntries` was deliberately left without it because the `/save`
command-line path legitimately discards its result. No cast sweep and no reformatting,
following HRC section 18.

Rebuild and re-validation. All four configs build clean (Debug and Release, Win32 and
x64). The rebuild launches and its toolbar empty area reads 240,240,240. The save core
and the `/save` path were not touched and keep their earlier byte-identical validation
against the original.
