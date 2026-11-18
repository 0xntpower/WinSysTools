# SimpleWMIView -- reverse-engineering notes

Target: `targets/simplewmiview-x64/SimpleWMIView.exe` -- NirSoft SimpleWMIView
v1.56, WMI (WQL) query viewer. **x64 PE, Unicode, not packed.**
md5 `651bffaeeef1688bfd368f063764e228`,
sha256 `66e497284b985eac4a689084640db8ff4554a945ca604a33010c29be218e4d4e`.
Image base 0x140000000. 376 functions, 265 strings (IDA). Engines used -- IDA
idalib session `simplewmiview`, plus rea/Ghidra 12.1.2 snapshot
`analysis/snapshots/simplewmiview.snap` with a `rea decompile` cross-check of the
query core.

## WMI query core (confirmed with rea)
The connect-and-query path is two functions. `sub_140017218` is ConnectServer
and `sub_140017508` is the query plus enumeration. Both were confirmed against
the rea decompile of `sub_140017508`.

```
# sub_140017218 (connect)
CoCreateInstance(CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                 IID_IWbemLocator, &locator)
locator->ConnectServer(namespace, user, password, 0, 0, 0, 0, &services)   # vtbl +24

# sub_140017508 (query)
if the class field has no space:  GetObject(class, ...)                     # vtbl +48
services->ExecQuery(L"WQL", query, 0x30, 0, &enum)                          # vtbl +160
CoCreateInstance(CLSID_WbemObjectTextSrc, ..., IID_IWbemObjectTextSrc, &ctx+8)
enum->Next(...) -> IWbemClassObject                                         # vtbl +32 (sync)
obj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY=64)                          # vtbl +64
obj->Next(0, &name, &value, &cimtype, 0)  (per property)                    # vtbl +72
obj->EndEnumeration()                                                       # vtbl +80
```

The ExecQuery flags `0x30` are `WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY`.
The query language is the three-character `WQL` at 0x14001b9f0. The default
namespace is `root\CIMV2` and the default class is `Win32_Process`, both read
from the settings store. A class name with no space is wrapped as `SELECT * FROM
<class>`, and a class field that contains a space is passed through as a full WQL
query.

GUIDs recovered from `.rdata` (raw bytes), matching `wbemidl.h`:
- rclsid @0x1400199A0 = **CLSID_WbemLocator** `{4590F811-1D3A-11D0-891F-00AA004B2E24}`
- riid   @0x1400198D0 = **IID_IWbemLocator** `{DC12A687-737F-11CF-884D-00AA004B2E24}`
- rclsid @0x140019C50 = **CLSID_WbemObjectTextSrc** `{47DFBE54-CF76-11D3-B38F-00105A1F473A}`
- riid   @0x140019BD0 = **IID_IWbemObjectTextSrc** `{5E97458A-CF77-11D3-B38F-00105A1F473A}`

The two sync modes in the original are an asynchronous path (NextAsync with a
custom IWbemObjectSink at `off_14001BA50` plus a message pump) and a synchronous
path (Next). Both read the same fields and produce the same records. The
recreation implements the synchronous path, which is the one used for headless
export and yields identical data.

## Dynamic columns and per-property value handling
SimpleWMIView has no fixed column set. The columns are the property names of the
result objects. For a plain class the column set comes from `GetObject(class)`
read by `sub_140017428`. For a free WQL query the column set comes from the first
returned object. Each property value is normalised by its CIM type in
`sub_140002B04`, which the recreation follows in `WmiQuery.cpp::makeCell`:

| CIM type | VARIANT | stored as |
|----------|---------|-----------|
| CIM_DATETIME (101) | VT_BSTR | parsed to a UTC FILETIME |
| CIM_SINT64 / CIM_UINT64 (20/21) | VT_BSTR | parsed back to a 64-bit number |
| CIM_STRING / CIM_REFERENCE / CIM_CHAR16 | VT_BSTR | the string value |
| CIM_BOOLEAN (11) | VT_BOOL | True or False |
| 8/16/32-bit integers | VT_I*/VT_UI* | decimal number |
| CIM_REAL32 / CIM_REAL64 | VT_R4/VT_R8 | fixed-point number |
| any array (0x2000 flag) | VT_ARRAY | elements joined with `, ` |

WMI hands 64-bit integers and datetimes back as strings, so the original parses
them back (`swscanf("%I64d")` for 64-bit, and a SWbemDateTime object for the
datetime). The datetime is stored as a FILETIME and rendered by `sub_1400082CC`
with GetDateFormatW short-date plus GetTimeFormatW long-time, local unless Show
Time In GMT is on, with an optional `.%3.3d` milliseconds field. The CIM type of
each column is also used for the field-type name shown in the status bar when the
mouse is over a column header. The type names are string-table ids 2000 + type
(for example 2003 is 32-Bit Signed Integer, 2101 is Date Time).

## Config -- `HKCU\Software\NirSoft\SimpleWMIView` (and optional .cfg INI via /cfg)
Value names and defaults recovered from the settings table `sub_1400020F8`:

| Value | default | Value | default |
|-------|---------|-------|---------|
| Namespace | `root\CIMV2` | Class | `Win32_Process` |
| ConnectComputer | 1 | ComputerName | (empty) |
| SyncMode | 1 (async) | Timeout | 15000 |
| UserName | (empty) | ShowGridLines | 0 |
| ShowInfoTip | 1 | MarkOddEvenRows | 0 |
| AutoColumnsSize | 0 | AutoFillClassesCombo | 1 |
| ClassesAutoComplete | 0 | AlignNumbersToRight | 0 |
| ShowTimeInGMT | 0 | ShowMilliseconds | 0 |
| BlackBackground | 0 | AutoUpdateOnClassChange | 0 |
| EnumerateFullNamespaceTree | 1 | SaveFileEncoding | 0 |
| SaveFilterIndex | 0 | TrayIcon | 0 |
| StartAsHidden | 0 | AlwaysOnTop | 0 |
| MainFont | (LOGFONT) | UseQuickFilter | 0 |
| QuickFilterString | (empty) | QuickFilterColumnsMode | 1 |
| QuickFilterFindMode | 1 | QuickFilterShowHide | 1 |
| QuickFilterCaseSensitive | 0 | | |

Also `Sort` (signed column index) and `WinPos` (WINDOWPLACEMENT). SyncMode 1 is
Asynchronous Query and 2 is Synchronous Query. ConnectComputer 1 is this computer
and 2 is a remote computer reached as `\\<name>\<namespace>`.

## Command-line switches
`/Namespace`, `/Class`, `/ConnectComputer <1|2>`, `/ComputerName`, `/SyncMode
<1|2>`, `/Update`, `/cfg <file>`, `/sort <column|~column>` (repeatable, by index
or name), `/Columns <comma list>` (export or display a subset of properties by
name), `/SaveDirect`, `/RunAsAdmin`, `/savelangfile`, plus the save switches
`/stext /stab /stabular /scomma /shtml /sverhtml /sxml /sjson`. An empty filename
sends the save to stdout.

## Export formats and encodings (confirmed by running the original)
The default encoding (SaveFileEncoding 0) is UTF-16LE with a BOM for every format
except the comma-delimited file, which is ANSI with no BOM. The Save File
Encoding option can force ANSI, Unicode or UTF-8 instead.
- `/stext`: each record is bracketed by a 50-`=` rule and a blank line. Field
  names are left-justified to a minimum width of 18, then `: `.
- `/stab`: a header row of property names, then one tab-separated row per record,
  with no trailing tab (the v1.55 fix).
- `/stabular`: fixed-width columns.
- `/scomma`: a header row, comma-separated, values quoted when they contain a
  comma, quote or line break.
- `/shtml` and `/sverhtml`: a NirSoft HTML table, horizontal or vertical per
  record.
- `/sxml`: `<?xml version="1.0" encoding="ISO-8859-1" ?>` with one `<item>` per
  record and slugged tag names.
- `/sjson`: an array of objects keyed by the property names.

## UI (resources)
Main menu 102, context menu 104, tray menu 113, accelerators 103. Dialogs --
Properties 105, Exception 107, the embedded WMI options bar 111 (namespace, class
or query, computer, synchronous mode, timeout, Update), About 112, Quick Filter
114, Column Settings 1096. Toolbar bitmap 110 is a 24-bit 112x16 strip of seven
16x16 images. The toolbar is eight buttons in the original (a leading separator,
then Save 40001, Refresh 40004, Copy 40008, Properties 40009, Find 40014, a Sort
By dropdown 49902, and Exit 40002), recovered from `sub_14000FAF4`. Status bar
shows `%d item(s)`, `, %d Selected` and the query time in milliseconds. Commands
40001..40071 plus the encoding radio group 45700..45703.

## Reconstruction
Clean C++20 plus WTL under `src/SimpleWMIView/`, x64 Unicode, output
`SimpleWMIView.exe`. Files: `SimpleWMIView.cpp` (WinMain plus the command line),
`WmiResult.h` (the dynamic-column model and per-cell formatting), `WmiQuery.cpp/.h`
(the WMI engine, ATL CComPtr and CComBSTR), `Export.cpp/.h` (the eight save
formats and the encodings), `Config.h` (registry plus .cfg), `RegKey.h`,
`MainFrame.h`, `OptionsBar.h` (the embedded options bar), `Dialogs.h` (About,
Properties, Column Settings), `resource.h`, `res/SimpleWMIView.rc`. Links ole32,
oleaut32, wbemuuid, comctl32, shell32, advapi32, comdlg32, gdi32, version. The
ProjectGuid is kept as `{7A8D6580-69F0-4E15-953A-2596CFFD03A5}` and the stub
`main.cpp` was deleted.

The shared toolbar helper `common/UiUtil.h` masks the strip corner colour
transparent. The main window hosts it on a plain (non-flat) toolbar rather than a
flat one. A flat toolbar outside a rebar draws its background transparent, and on
the default WTL frame class (COLOR_APPWORKSPACE, dark on Windows 11) that left
both the strip and the area beside the buttons on a black box. A plain toolbar
paints an opaque button-face background, so the masked icons and the toolbar
blend. The frame class also uses COLOR_BTNFACE as a belt-and-suspenders choice.
This is the first reconstructed tool whose toolbar is actually sized and shown,
so this was the first time the 24-bit strip rendering was exercised.

## Deviations
- The asynchronous query path (NextAsync with a custom sink and a message pump)
  is documented but not implemented. The synchronous Next path is used, which is
  the default for headless export and returns identical records.
- Embedded-object properties (CIM_OBJECT) are not expanded through
  IWbemObjectTextSrc. They are rare in the common classes.
- Quick Filter, the tray icon, Start As Hidden, Always On Top, font selection,
  Enumerate Full Namespace Tree, remote credentials beyond the basic user and
  password fields, Run As Administrator and `/savelangfile` are not reconstructed.
- Per-class column width and order persistence (the original saves a layout keyed
  to the class) is simplified. The recreation keeps the full column set in its
  property order and lets Choose Columns pick a subset per session.
- The recreation uses a stable sort. The original uses `qsort`, which is not
  stable, so records with an identical sort key can appear in a different relative
  order. Field values are identical.

## Status -- DONE, validated
- Debug and Release both build for **x64** (VS2022 v143 toolset, the one with ATL
  on this box, driven by MSBuild from PowerShell).
- Validation query `SELECT * FROM Win32_OperatingSystem` through the headless
  export, diffed against the original:
  - `/stext`: byte-for-byte identical file size (4378 bytes), 68 lines, zero
    non-volatile differences.
  - `/stab`: identical header and 64 columns, zero non-volatile value differences.
  - `/scomma`: identical file size (1662 bytes), ANSI with no BOM like the
    original, zero non-volatile differences.
  - The only fields that differ are the inherently volatile ones
    (FreePhysicalMemory, FreeVirtualMemory, LocalDateTime to the second,
    NumberOfProcesses). Stable fields including InstallDate and LastBootUpTime
    match, which confirms the datetime and array handling.
- GUI launches, runs the default `Win32_Process` query, shows 45 dynamic columns
  and 860 rows with the status bar `860 item(s)` and the query time, and the
  toolbar shows its icons rather than a black box. Screenshot at
  `analysis/SimpleWMIView-gui.png` and the top strip at
  `analysis/SimpleWMIView-toolbar.png`.

## Fidelity and HRC pass (2026-11-17)

**Config storage -- FIXED (location).** The original persists settings in
`SimpleWMIView.cfg` next to the executable, section `[General]`, not in the registry.
Confirmed in the path builder `sub_140011B78` and the value table `sub_1400020F8`: the
binary imports only `GetPrivateProfile*`/`WritePrivateProfile*` and no registry API. The
recreation had stored config under `HKCU\Software\NirSoft\SimpleWMIView`. It now writes
the `.cfg` INI with the exact original format -- decimal integers, strings direct, and
`WinPos` as uppercase space-separated hex bytes (`%2.2X `). `/cfg` repoints the store.
Value names and defaults match `sub_1400020F8` (Namespace `root\CIMV2`, Class
`Win32_Process`, ConnectComputer 1, SyncMode 1, Timeout 15000, AutoFillClassesCombo 1,
EnumerateFullNamespaceTree 1, ShowInfoTip 1, and the rest 0). The per-class Columns blob
stays a documented simplification, so no Columns value is written.

**HRC v1.1 light touch.** The config was rewritten in HRC style -- `nullptr`,
`static_cast` over C-style casts, every local initialized, `[[nodiscard]]` on the shared
readers. The format lives in the shared helper `src/common/CfgIni.h`. The pre-existing
unscoped `SaveEncoding` and the cell `Kind` enum were left as is because scoping them
would ripple through value conversions and comparisons.

**Rebuild and re-validation.** Debug and Release x64 both build clean. Config
round-trips to `SimpleWMIView.cfg`: seeded ShowGridLines, MarkOddEvenRows and Timeout
20000 are preserved, Namespace and Class keep their defaults, `WinPos` is written as
`2C 00 00 00 ...` hex, and no registry key is created. The `/scomma` export still runs
and produces the correct property header (451666 bytes for Win32_Process here).
