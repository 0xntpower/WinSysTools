# ProductKeyScanner -- reverse-engineering notes

Target: `targets/productkeyscanner-x64/ProductKeyScanner.exe` -- NirSoft Product Key
Scanner v1.03, x64 PE, **Unicode**, not packed. md5 `28c87b50b10cfdf949735fd106375631`,
sha256 `dcccb2ff8bc0db7e054f55fdecf6dbf9dba785bc0abeecf17cc6af4f1302d897`. Image base
`0x140000000`, 370 functions, 259 strings. Engines -- IDA idalib session
`productkeyscanner` plus rea/Ghidra 12.1.2 snapshot `analysis/snapshots/productkeyscanner.snap`.

## The two decoders (the core)

Both recovered from **`sub_14000FB8C`** (the base-24 decode) and **`sub_14000FE18`**
(the blob dispatch), confirmed line-for-line by rea/Ghidra.

### base-24 DigitalProductId decode -- `sub_14000FB8C`
Alphabet `"BCDFGHJKMPQRTVWXY2346789"` (24 symbols, strcpy'd on the stack, so a raw byte
search for it lands in `.rdata` at `0x140017478` but has no code xref).

- Input is a 15-byte key region. If all 15 bytes are zero the function returns 0 (no key).
- **Windows 8+ detection** -- bit 3 (`& 8`) of the last region byte. When set, the decoder
  sets an `isWin8` flag and rewrites that byte as `(b & 0xF7) | ((b >> 1) & 0x78)`.
- The 15 bytes are treated as a big integer (byte 14 most significant) and divided by 24
  repeatedly, low symbol first, placed from display position 28 down to 0. Dashes land
  where the paired counter is a multiple of 6 (positions 5, 11, 17, 23), giving the
  classic 5x5 `XXXXX-XXXXX-XXXXX-XXXXX-XXXXX`.
- **Win8 'N' re-insertion** -- the most significant digit's numeric value becomes the
  index at which an `N` is inserted into the 24 remaining symbols, and the result is
  regrouped into five dash-separated groups of five.

Ported verbatim in `src/ProductKeyScanner/Decoder.cpp`.

### blob dispatch -- `sub_14000FE18`
Locates the 15-byte region inside the REG_BINARY blob by size:
- `len >= 0x338` -> region at offset **808** (DigitalProductId4), Product ID at blob+8 is **UTF-16**.
- `len == 16` -> region at offset **0**.
- `len >= 0x44` -> region at offset **52** (DigitalProductId), Product ID at blob+8 is **ANSI**.
- otherwise no key.

Product ID is read first from the registry `ProductId` value, falling back to blob+8.
Product Name is `ProductName`, else `ConvertToEdition`, else `ProductNameNonQualified`.

## Data sources

### Registry walk -- `sub_1400100E4`
A recursive walk of the `SOFTWARE` hive (live `HKLM\SOFTWARE`, or an offline hive on an
external drive), reading `DigitalProductId` and `DigitalProductId4` at every key, to depth
32, skipping four heavy subtrees by full path: `Software\Microsoft\Windows NT\CurrentVersion\Perflib`,
its `Wow6432Node` twin, `Software\Classes` and `Software\Wow6432Node\Classes`. The Registry
Key column is `HKEY_LOCAL_MACHINE\` plus the walked path.

### WMI -- `sub_140010AAC`
`ROOT\CIMV2`, two queries. `SELECT * FROM SoftwareLicensingProduct WHERE PartialProductKey
IS NOT NULL` fills Name -> Product Name, ProductKeyID -> Product ID, and a masked key
`XXXXX-XXXXX-XXXXX-XXXXX-` plus the 5-char PartialProductKey. `SELECT * FROM
SoftwareLicensingService WHERE OA3xOriginalProductKey IS NOT NULL` gives the full key.

### BIOS
The ACPI **MSDM** firmware table via a dynamically resolved `GetSystemFirmwareTable`
(`sub_140004474` resolves it). The 29-char OEM key sits at table offset 56.

## Columns (STRINGTABLE 1001..1007, title array at .rsrc 0x140023100)
Display order 0..6 -- **Product ID, Product Key, Product Name, Scan Type, Registry Key,
Registry Value, Modified Time**. Scan Type values -- Registry (621), BIOS (622), WMI (623).

## Config -- `HKCU\Software\NirSoft\ProductKeyScanner`
`MarkOddEvenRows`, `ShowGridLines` (default 1), `SaveFilterIndex`, `ShowInfoTip`,
`AddExportHeaderLine`, `DataSource` (0 current system, 1 external hive), `SoftwareHivePath`,
`UseBIOS`, `UseWMI`, `MainFont` (LOGFONT), `WinPos` (WINDOWPLACEMENT), `Sort`, `Columns`.

## Command line
Recovered switches -- `/nosaveload`, `/cfg`, `/savelangfile`, `/deleteregkey`. The tool
has **no `/s*` export switches** (`strings -t x` finds none, and the readme describes
export as GUI only). The recreation adds `/stext /stab /sjson /shtml /sverhtml /sxml
/scomma <file>` and a `/selftest` decoder check as deliberate improvements, mirroring rdpv.

## UI
Menus 102 (main) and 104 (context). Accelerators 103. Dialogs 105 Properties, 107 Exception,
112 About, 113 Scan Options, 1096 Column Settings. Toolbar bitmap 110 (128x16, eight glyphs).
Save formats (STRINGTABLE 501..507) -- Text, Tab, JSON, HTML Horizontal, HTML Vertical, XML,
Comma Delimited. On startup the original shows the Scan Options dialog before scanning.

## Reconstruction
Clean C++20 + WTL under `src/ProductKeyScanner/`, building `ProductKeyScanner.exe` for x64.
Files -- `Decoder.{h,cpp}` (the two decoders), `Scanner.{h,cpp}` (registry walk, WMI, BIOS),
`ProductKey.h`, `Columns.h`, `RegKey.h`, `Export.{h,cpp}` (seven formats), `Dialogs.h`
(About, Scan Options, Column Settings, Properties), `MainFrame.h` (frame, worker-thread
scan with Esc cancel, custom-draw odd/even rows, font picker), `ProductKeyScanner.cpp`
(entry, CLI, selftest), `resource.h`, `res/ProductKeyScanner.rc`.

The scan runs on a worker thread so the UI stays responsive, Esc can cancel it, and the
thread owns its own COM apartment instead of touching the UI thread's.

## Status -- DONE

- **Build** -- Debug and Release **x64** both build clean (one harmless `/Ob2`->`/Ob3` D9025
  warning in Release). ProjectGuid kept `{3653B229-EBC7-4923-9CB9-F3AEBE030D85}`, stub
  `main.cpp` removed. The comctl6 + dpiAware manifest is supplied by the linker plus a
  `/manifestdependency` pragma to avoid a duplicate RT_MANIFEST (the original embeds its own).
- **Decoder cross-checks (four independent)** --
  1. rea/Ghidra decompile of `sub_14000FB8C` matches the IDA decompile exactly.
  2. An independent Python reference decoder (written with a different structure) decodes
     the live registry `DigitalProductId`, `DigitalProductId4` and `DefaultProductKey`
     blobs to the same keys the recreation produces.
  3. Windows' own WMI `PartialProductKey` reports the last five chars as `6Q84J`, matching
     the last group of the base-24 decode of the Windows `DigitalProductId`.
  4. The original binary's GUI shows the identical keys (see below).
- **/selftest** -- exit 0. Two known-answer vectors (classic and the Win8 'N'-insertion
  path), generated offline by an independent base-24 encoder, both assert correctly through
  `DecodeProductKeyRegion` and `DecodeProductKeyBlob`.
- **/s diff against the original** -- the original v1.03 has no CLI export, so the check is
  the recreation's `/stext` against the original's populated GUI grid. Both produce the same
  ten rows with identical Product Keys, Product IDs, Scan Types, Registry Keys, Registry
  Values and Modified Times. Representative keys on this machine:
  `NRG8B-VKK3Q-CXVCJ-9G2XF-6Q84J` (Windows 10 Pro for Workstations),
  `XGVPP-NMH47-7TTHJ-W3FW7-8HV2C` (DefaultProductKey), and the WMI masked keys ending
  `6Q84J` and `XQBR2`.
- **GUI** -- launches with no crash, scans, populates all seven columns with per-row key
  icons and grid lines. Screenshot `analysis/productkeyscanner-gui.png` (recreation),
  `analysis/productkeyscanner-orig-gui.png` (original, for side-by-side).

### Deviations (all deliberate, noted)
- `/stext` and the other `/s*` export switches plus `/selftest` are added (the original has
  no CLI export). This matches the rdpv precedent.
- Default column widths are the recreation's own sensible values (the original stores them
  in its `Columns` config blob, which is not reproduced byte-for-byte).
- The dynamic "Sort By" submenu and the Hebrew `Stop` menu placeholder (resource 111) are
  not reproduced. Header-click sort and the Esc cancel cover the function.
- Export files are written UTF-8 with a BOM.

## Fidelity and HRC pass (2026-11-17)

**Config storage -- FIXED (location and format).** The original persists settings
in `ProductKeyScanner.cfg` next to the executable, section `[General]`, not in the
registry. Confirmed in the save routine `sub_14000D300` and the value table
`sub_1400021BC`: the binary imports only `GetPrivateProfile*`/`WritePrivateProfile*`,
its single `RegOpenKeyExW` is the product-key data scan, and no `RegSetValueEx` or
`RegCreateKeyEx` exists. The recreation had stored config under
`HKCU\Software\NirSoft\ProductKeyScanner`. It now writes the `.cfg` INI with the exact
original format: decimal integers, `SoftwareHivePath` as a string, and `MainFont`,
`Columns` and `WinPos` as uppercase space-separated hex bytes (`%2.2X `). `Columns`
is now the original `(WORD width, WORD columnIndex)` pair layout rather than the
previous raw index array. `/cfg` repoints the store, and `/deleteregkey` still clears
the legacy registry root. Value names and defaults match `sub_1400021BC`:
MarkOddEvenRows, ShowGridLines, SaveFilterIndex, ShowInfoTip 1, AddExportHeaderLine 1,
DataSource 1, SoftwareHivePath, UseBIOS 1, UseWMI 1, MainFont, plus WinPos, Columns,
Sort. DataSource 1 is current system and 2 is external hive, the NirSoft one-based
convention that matches LoadFrom in the sibling tools.

**HRC v1.1 light touch.** The config load and save were rewritten in HRC style --
`nullptr`, `static_cast` over C-style casts, every local initialized at declaration,
`[[nodiscard]]` on the shared readers, and no new unscoped enum. The format lives in
the new shared helper `src/common/CfgIni.h`. Pre-existing unscoped enums were left
alone because scoping them would ripple across initializers and comparisons, which the
light-touch rule warns against.

**Rebuild and re-validation.** Debug and Release x64 both build clean (only the known
`/Ob2`->`/Ob3` D9025 note). `/selftest` exits 0 (decoder intact, synthetic vectors,
no real keys handled). Config round-trips to `ProductKeyScanner.cfg`: seeded
ShowGridLines, MarkOddEvenRows, Sort, UseWMI and DataSource are preserved, `Columns`
is written as width/index pairs that decode to the default widths
(`78 00 00 00` width 120 index 0, `BE 00 01 00` width 190 index 1, and so on), `WinPos`
is a 44-byte WINDOWPLACEMENT in `2C 00 00 00 ...` hex, and no registry key is created.

## Header/source split (2026-11-18)

**Code motion -- MainFrame.h to MainFrame.cpp.** The `CMainFrame` method bodies moved
out of `MainFrame.h` into a new `MainFrame.cpp`, following the WinObj template
(commit 4a3adc0). The header now carries only the class shape -- members, the
`DECLARE_FRAME_WND_CLASS` macro, the message map, the nested `ScanArg` type, the two
`constexpr` roots, and the method declarations. Every body moved verbatim in the same
order, with `override` and `static` dropped on the out-of-line definitions and kept on
the declarations. The WTL handler signatures (trailing `BOOL&`) are preserved, and the
`OnEraseBkgnd` to `FillFrameBtnFace` call, `PinToolbarLight`, and `PinToLightMode` are
byte-identical. No other tool and no `.sln` were touched.

- `MainFrame.h` -- 667 lines before, 190 after (declarations only).
- `MainFrame.cpp` -- new, 797 lines (HRC clang-format, Allman, one-liners expanded).
- `MainFrame.cpp` added to the `ProductKeyScanner.vcxproj` ClCompile group.
- clang-format `--style=file` applied to both files.
- Rebuild `/t:Rebuild` -- Debug and Release x64 both clean (the known `/Ob2`->`/Ob3`
  D9025 note only). MainFrame.cpp compiles as its own translation unit.

**GUI re-validation (counts and layout only, no key values captured).** The rebuilt
Release exe launched with `/nosaveload`, the startup Scan Options dialog accepted with
IDOK, and the scan ran to completion. The ListView populated **10 rows** across **7
columns** (the Product ID, Product Key, Product Name, Scan Type, Registry Key, Registry
Value, Modified Time layout). No cell value was read -- only `LVM_GETITEMCOUNT` and the
header item count, both scalar. The toolbar child
(`FindWindowExW(frame, 0, "ToolbarWindow32", null)`) empty band sampled **240,240,240**,
so the dark-toolbar light fix holds. Frame geometry is the unchanged `CreateEx` default
and is identical by construction, since OnCreate and the entry CreateEx call were not
modified. No product key was captured, logged, or shown. The decoder was not re-run on
real data -- this is code motion only and the known-vector round-trip from the original
validation stands.
