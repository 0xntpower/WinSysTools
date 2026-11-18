# OfficeIns -- reverse-engineering notes

Target: `targets/officeins-x64/OfficeIns.exe` -- NirSoft OfficeIns v1.20, x64 PE,
**ANSI/MultiByte internally** (every import is the `...A` variant), not packed. Image
base 0x140000000. md5 `bb9b8b3c2611d80c76e2fbcc6fdf5589`. 246 functions, 343 strings.
Build string in the binary -- `c:\Projects\VS2005\OfficeIns\x64\Release\OfficeIns.pdb`.
Engines used -- IDA idalib session `officeins` (primary), rea/Ghidra snapshot
`analysis/snapshots/officeins.snap` (cross-check).

OfficeIns lists Microsoft Office COM / automation add-ins found across the registry,
resolves each add-in's CLSID to its server file, reports the LoadBehavior-derived
start mode and connected state, and lets the user change them (it writes LoadBehavior).

## Entry / startup
- `start` @0x14000e3c0 -- VC-style CRT (`__set_app_type`, `_initterm`, `__getmainargs`).
- The GUI builds the main window (WNDCLASS "OfficeIns", menu 102, toolbar bitmap 104,
  status bar, a report ListView). The CLI "save and quit" path lives in `sub_14000B8EC`.

## Registry enumeration
Driver `sub_140002FAC(ctx)` loops the **eight Office applications** in this order
(appIndex 1..8), stored on each record as the Software column source:

    Word, Excel, Outlook, PowerPoint, MS Project, Access, Visio, FrontPage

For each application it scans **six registry layouts** (`%s` = application name), via
`sub_140002F1C`, which for every layout scans **HKCU first, then HKLM**:

    Software\Microsoft\office\%s\Addins
    Software\Microsoft\office\8.0\%s\Addins
    Software\Microsoft\office\9.0\%s\Addins
    Software\Microsoft\office\10.0\%s\Addins
    Software\Microsoft\office\11.0\%s\Addins
    Software\Microsoft\%s\Addins

The two hive constants stored per record are `0x80000001` (HKCU) and `0x80000002`
(HKLM). With `/RemoteComputer`, `RegConnectRegistryA` connects HKCU, HKLM and HKCR on
the remote box, otherwise the local predefined hives are used.

Per Addins key, `sub_140002C8C` enumerates the ProgID subkeys. For each ProgID it reads:
- value `Description`, and when that is empty, value `FriendlyName` -> Description column
- value `LoadBehavior` (REG_DWORD) -> Start Mode and Connected columns

### CLSID resolution (`sub_140002874`)
- read the default value of `HKCR\<ProgID>\CLSID` -> the CLSID. When empty the record
  stays minimal and no file is resolved.
- open `HKCR\CLSID\<CLSID>`, try subkey `InprocServer32`, else `LocalServer32`.
- read that subkey's default value -> the server path. A quoted path is unwrapped at the
  quotes. A LocalServer32 path is trimmed of arguments after the last dot.
- when the file's basename is `mscoree.dll` (case-insensitive) the add-in is managed.
  `.Net Add-In` = Yes, the real assembly is taken from the `CodeBase` value (`file:///`
  prefix stripped, `/` turned into `\`), and `RuntimeVersion` of the first InprocServer32
  version subkey fills the `.NET Version` column.

### File version info (`sub_1400026B8` -> `sub_14000E068`)
From the resolved server file, OfficeIns reads ProductName, FileVersion, CompanyName and
FileDescription, plus the file's creation time (local). On a remote scan the path is
rewritten to the admin-share form `\\computer\C$\...`.

## LoadBehavior semantics (recovered exactly)
The display classifier `sub_140001BC4` and the writer `sub_140002580` give the full
meaning of LoadBehavior.

Start Mode column, tested in this order:
- bit 1 set (value & 2)     -> "Load at Startup"   (string 1201)
- bit 3 set (value & 8)     -> "Load on Demand"    (string 1202)
- bit 4 set (value & 0x10)  -> "Load at First Time"(string 1203)
- value <= 1                -> "Disabled"          (string 1200)

Connected column -- bit 0 (value & 1) -> "Yes" (15) else "No" (16).

The writer `sub_140002580` does `new = OR | (old & ~AND)` and writes it back. The menu
and command-line masks (`sub_14000A644`):
- Start Mode: Disabled OR=0 AND=254, Load at Startup OR=2 AND=254, Load on Demand OR=8
  AND=254, Load at First Time OR=16 AND=254 (AND=254 preserves only the connected bit)
- Connected: No OR=0 AND=1, Yes OR=1 AND=1

## Record field map (verified against the getter `sub_140001BC4`)
| off   | field                | source |
|-------|----------------------|--------|
| +16   | ProgID               | Addins subkey name |
| +272  | Description          | value Description, else FriendlyName |
| +528  | appIndex (dword)     | 1..8, chooses the Software name (strings 1101..1108) |
| +536  | Product Name         | version ProductName |
| +792  | Version              | version FileVersion |
| +1048 | Company              | version CompanyName |
| +1304 | File Description     | version FileDescription |
| +1560 | .NET Version         | managed RuntimeVersion |
| +1816 | File Created Time    | file creation time (local FILETIME) |
| +1824 | CLSID                | default of HKCR\<ProgID>\CLSID |
| +2080 | Filename             | InprocServer32 / LocalServer32 server path |
| +2344 | root hive (qword)    | 0x80000001 HKCU or 0x80000002 HKLM |
| +2352 | registry subpath     | "<Addins path>\<ProgID>" |
| +2616 | LoadBehavior (dword) | Start Mode + Connected |
| +2620 | .Net Add-In (dword)  | 1 when hosted by mscoree.dll |

## Columns (15, titles STRINGTABLE 1001..1015, NOT in numeric order)
The display order is fixed by `sub_140001BC4`. Each index returns one record field and
the setup assigns it a title. Default width 100 (confirmed by OfficeIns.cfg). Order:

    0 ProgID  1 Description  2 Software  3 Start Mode  4 Connected  5 Product Name
    6 Version  7 Company  8 File Description  9 File Created Time  10 Filename
    11 CLSID  12 .Net Add-In  13 .NET Version  14 Registry Key

The Registry Key column renders `HKEY_CURRENT_USER\...` or `HKEY_LOCAL_MACHINE\...` from
the stored hive constant and subpath. File Created Time formats with
LOCALE_SYSTEM_DEFAULT (0x0800), short date plus default time (`sub_140004F00`), and
renders "N/A" when there is no file or the year is 1985 or earlier.

## Menus / commands
- Menu 102, context menu 104, accelerators 103. File commands -- Change Start Mode
  submenu (42000..42003), Change Connected Mode submenu (42100..42101), Open AddIn In
  RegEdit (40023, F7), Open CLSID In RegEdit (40025, F8), Save Selected (40001), File
  Properties (40022), Properties (40009), Exit (40002). Edit -- Find (40014), Copy
  (40008), Select All (40007), Deselect All (40015). View -- Grid Lines (40005), HTML
  Report All / Selected (40006 / 40010), Choose Columns (40011), Auto Size (40012),
  Refresh (40004). Help -- About (40003).
- Dialogs -- Properties 105, Exception 107, About 112, Column Settings 1096.

## Config -- OfficeIns.cfg (INI, next to the exe, since v1.05)
Section `[General]` -- `ShowGridLines`, `SaveFilterIndex`, `WinPos` (WINDOWPLACEMENT as
space-separated hex bytes), `Columns` (per visible column a little-endian width then
column index, four bytes each), `Sort` (sort column). The legacy registry root
`Software\NirSoft\OfficeIns` is still present in the binary.

## Export / CLI (`sub_14000B8EC`)
Save formats (STRINGTABLE 501..506) -- Text, Tab delimited, Tabular, HTML Horizontal,
HTML Vertical, XML. Report title "Office Add-Ins List" (string 8), footer "Created by
using" (string 6). Save title "Select a filename to save" (string 7). Status line
"%d add-in(s)" (string 4) plus ", %d Selected" (string 5).

Command-line switches recovered -- `/stext /stab /stabular /shtml /sverhtml /sxml <file>`
(save and quit), `/StartMode <0-3> <name>`, `/Connected <0-1> <name>` (name matches
ProgID, CLSID, full path, or dll basename -- `sub_140001B04`), `/RemoteComputer <\\name>`,
`/savelangfile`, `/noloadsettings`. The Tabular format has no header row and truncates
each field to a fixed per-column width (measured -- 51 81 31 21 11 51 51 51 51 31 121 41
11 11 61). Text format wraps each record in a 50-char `=` line above and below, titles
padded to the widest title and separated by " : ".

## Reconstruction
Clean C++20 + WTL under `src/OfficeIns/`, x64 Unicode, built to `OfficeIns.exe`.
- `Addin.h` / `AddinEnumerator.cpp` -- the record and the full registry scan, CLSID
  resolution, managed-add-in detection and version-info read, plus the LoadBehavior
  read-modify-write for Start Mode and Connected.
- `Columns.h` -- the 15 columns and the fixed tabular widths.
- `Export.cpp` -- the six report formats, written in the system ANSI code page to match
  the ANSI original byte for byte.
- `MainFrame.h` -- the WTL frame (owner-data ListView, toolbar, status bar, menus,
  Change Start Mode / Connected, RegEdit jump, Find, Choose Columns, HTML report).
- `Dialogs.h` -- About, Properties, Column Settings over the extracted templates.
- `RegKey.h` -- a small RAII registry wrapper.
- `OfficeIns.cpp` -- WinMain and the command-line handling.
- `res/OfficeIns.rc` -- the extracted resources (includes prepended, letter
  accelerators quoted).

## Cross-check (rea / Ghidra)
`rea decompile` of the enumeration core `sub_140002C8C` matches the IDA Hex-Rays output
line for line: open root\subpath with KEY_READ, `RegEnumKeyExA` over the ProgID
subkeys, read `Description` and fall back to `FriendlyName` when empty, read the 4-byte
`LoadBehavior`, build the record (vtable `PTR_FUN_14000fab0`), resolve the CLSID via
`sub_140002874`, store the hive constant, the "%s\%s" registry subpath, the app index
and LoadBehavior, then append. Snapshot at `analysis/snapshots/officeins.snap`.

## Status
DONE. Debug and Release both build for x64. Validation on a machine with 18 live Office
add-ins. The `/stext`, `/stab` and `/stabular` exports are **byte-identical** to the
original (SHA-256 match on all three), including the populated version-info rows for the
TeamViewer and Skype for Business add-ins and the N/A rows for the native ones. The
enumeration order matches the original exactly. The GUI launches with no crash, shows all
15 columns in the recovered order and "18 add-in(s)" in the status bar. Screenshot at
`analysis/officeins-gui.png`. Two fixes landed the byte match. The record separator is
" : " with the title padded to the widest column, each record is bracketed by a 50-char
separator, the date column uses LOCALE_SYSTEM_DEFAULT with DATE_SHORTDATE, `/stab`
appends a trailing tab, and Tabular uses fixed truncating widths with no header.
Deliberate deviation. The recreation is a Unicode build using W registry APIs rather
than the ANSI original, which does not affect the ANSI-encoded export bytes.

## Fidelity and HRC pass

Config storage (already correct). The original stores settings in `OfficeIns.cfg` next
to the exe, section `[General]`, which the earlier notes recovered in full. A string
scan confirms the `.cfg` and `General` names and also shows the legacy
`Software\NirSoft\OfficeIns` registry string, which is present but superseded by the
file store since v1.05, the same vestigial pattern seen in VNCPassView and rdpv. The
recreation already writes `OfficeIns.cfg` with section `[General]` and matching value
names, so config storage was left unchanged.

HRC v1.1 light touch. Added `[[nodiscard]]` to `FormatFromSwitch`, `EnumerateAddins`, and
`AddinMatchesName`, whose results are load bearing. `SetStartMode` and `SetConnected`
were left as they are because their status is sometimes discarded by the menu handlers.
No cast sweep and no reformatting, following HRC section 18.

Rebuild and re-validation. All four configs build clean (Debug and Release, Win32 and
x64). The rebuild launches without elevation and its toolbar empty area reads
240,240,240. The six export formats were not touched and keep their earlier
byte-identical validation against the original.
