# RemoteDesktopPassView (rdpv) -- reverse-engineering notes

Target: `targets/rdpv/rdpv.exe` -- NirSoft Remote Desktop PassView v1.02, x86 PE,
**ANSI/MultiByte**, VS2005-era (`c:\Projects\VS2005\rdpv\Release\rdpv.pdb`). The shipped
`rdpv.exe` is UPX-packed. Analysis ran against `rdpv.unpacked.exe` (already unpacked),
sha256 `6f815e1ae2b3a43c83961ba7ef3fdde93fe2053454498816349c7af0eb6bd28a`, image base
0x400000, 308 functions, 299 strings.

Engines used -- IDA idalib session `rdpv` (primary, Hex-Rays), cross-checked with rea to
Ghidra 12.1.2 snapshot `analysis/snapshots/rdpv.snap`. Uses the larger NirSoft shared
framework (owner-data list, column manager, language-file loader, HTML/XML/CSV export
engine, exception dialog) rather than the tiny MIMEView-era shell.

Domain -- Remote Desktop PassView reveals the password stored by Microsoft Remote
Desktop Connection inside `.rdp` connection files. The stored value is a DPAPI blob,
recoverable only in the context of the user who created the file.

## Entry / startup
- `start` @0x408aaa -- VS2005 CRT, then WinMain `sub_4070E8`.
- `sub_4070E8` parses args, handles `/savelangfile` and `/deleteregkey` (both exit), then
  creates the main window `sub_406ED0`, shows it, posts message 0x415 (initial load) and
  runs the message loop.
- `sub_406ED0` registers class **`RemoteDesktopPassView`**, title **`Remote Desktop
  PassView`**, style WS_OVERLAPPEDWINDOW, 640x480. WndProc `sub_40156C` delegates to a
  framework C++ object. WM_CREATE build is `sub_405FD4` (menu 102, status bar id 0x101,
  toolbar id 0x102, SysListView32 id 0x103, `DragAcceptFiles`).

## Core -- the .rdp parse and the DPAPI recovery
The parse core is `sub_408161` (per file) feeding `sub_407FCE` (per line), cross-checked
with rea to Ghidra (identical structure, see below).

- `sub_408161` opens the `.rdp`, ignores files of 0x4000 bytes or larger, reads the whole
  file and treats it as **UTF-16LE** (line reader `sub_407F0E` walks `_WORD`s, splitting
  on CR/LF). Each line is split on the **first two colons** into key, type and value
  (`key:type:value`). Lines with fewer than two colons are skipped.
- `sub_407FCE` matches the key case-insensitively:
  - `full address`  -> Computer Name field (parse-record offset +0x000)
  - `domain`        -> Domain field        (offset +0x100)
  - `username`      -> User Name field     (offset +0x200)
  - `password`      -> Password field      (offset +0x300), matched by `wcsnicmp(key,
    "password", 8)` so the real key `password 51` matches on its first eight characters.
- Password value decode -- the value is a hex string, **two wide chars per byte** via
  `wcstoul(pair, 0, 16)`. The byte count accumulates in a local.
- DPAPI gate -- only when the decoded length is **greater than 0x20 bytes** does it build
  a `DATA_BLOB {cbData, pbData}` and call the decrypt helper `sub_401CD0`.
- `sub_401CD0` loads **crypt32.dll** by name, resolves **CryptUnprotectData**, and calls
  it as `CryptUnprotectData(pDataIn, NULL, NULL, NULL, NULL, 1, pDataOut)` --
  **no entropy**, no prompt struct, **dwFlags = 1 (CRYPTPROTECT_UI_FORBIDDEN)**.
- The decrypted output is **UTF-16 plaintext**. `sub_407F9F` stores it (the original caps
  a stored field at 126 wide chars), then `LocalFree`.

A row is produced whenever any of the four keys matched (`sub_408161` returns nonzero),
so `.rdp` files with connection info but no recoverable password still list, with an
empty Password column.

### rea / Ghidra confirmation of sub_407FCE (0x407fce)
Ghidra independently decompiled the same function and confirmed every load-bearing fact:
`_wcsicmp(param_2, L"full address")`, `L"username"`, `L"domain"`, and
`_wcsnicmp(param_2, L"password", 8)`, the `wcstoul(&pair, 0, 0x10)` two-char hex loop, the
`if (0x20 < count)` gate, and `FUN_00401cd0(&blob, &out)` DPAPI call followed by
`LocalFree`. Evidence id `ev_2facf6cdf756888f1a401c70cc7a9d1ac7c9a787d79c3e861a101c4c83a16069`.

## Default scan and ad-hoc files
- Default scan -- `sub_4019F4` (a virtual method) scans a single directory for `*.rdp`.
  The directory is **My Documents**. `sub_4085F8` resolves it via the dynamically loaded
  `SHGetSpecialFolderPathA(0, buf, 5, 0)` (**CSIDL_PERSONAL**), falling back to the
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders` value when the
  API is unavailable. Each found file is parsed by `sub_4018ED`.
- Open .rdp File (menu 40022) -- `sub_40654C` runs `GetOpenFileNameA` with the `*.rdp`
  filter (dialog title string 9 "Select Remote Desktop Filename", filter string 601
  "Remote Desktop Files"), parses the chosen file and refreshes the list.
- Drag and drop -- the WndProc handles WM_DROPFILES (0x233) through `DragQueryFileA` and
  parses the dropped file. `DragAcceptFiles(TRUE)` is set in WM_CREATE.

## Columns -- STRINGTABLE 1001-1005, display order confirmed by the GUI
| # | Title (STRINGTABLE) | source field |
|---|---------------------|--------------|
| 0 | Computer Name (1001) | `full address` |
| 1 | Domain (1002)        | `domain` |
| 2 | User Name (1003)     | `username` |
| 3 | Password (1004)      | DPAPI-recovered plaintext |
| 4 | Filename (1005)      | source `.rdp` path |

The side-by-side GUI run confirms this exact order (Computer Name, Domain, User Name,
Password, Filename).

## Config -- key `HKCU\Software\NirSoft\rdpv` confirmed
The config key name recovered from the binary is **`Software\NirSoft\rdpv`** (the
`/deleteregkey` path calls `RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\NirSoft\\rdpv")`).
Config value names present -- `WinPos`, `Sort`, `Columns`, `ShowGridLines`,
`SaveFilterIndex`, `ShowInfoTip`. The binary also carries the `.cfg` file path pieces
(`.cfg`, section `General`, `WinPos`, `Sort`, `Columns`) and `GetPrivateProfileStringA` /
`WritePrivateProfileStringA` imports, plus a `/noreg` switch. The readme (v1.01) states
configuration moved to an `rdpv.cfg` INI file. Default location is the registry key,
with `/noreg` selecting the INI file.

## Toolbar (sub_405FD4)
Bitmap 104 is 112x16 (7 tiles), `CreateToolbarEx` registers 6 images and 7 button slots.
Real buttons in order -- **Save (40001), Refresh (40004), Copy (40008), Properties
(40009), Find (40014), Exit (40002)**, with bitmap indices 1, 5, 2, 3, 0, 4.

## Commands / menus / dialogs / accelerators
- Menu 102 -- File (Open .rdp File 40022, Save Selected 40001, Properties 40009, Exit
  40002), Edit (Find 40014, Copy 40008, Select All 40007, Deselect All 40015), View (Show
  Tooltips 40025, Show Grid Lines 40005, HTML Report All 40006, HTML Report Selected
  40010, Choose Columns 40011, Auto Size Columns 40012, Refresh 40004), Help (About 40003).
- Context menu 104, Properties dialog 105, Exception dialog 107, About dialog 112, Column
  Settings dialog 1096.
- Accelerators 103 -- Ctrl+A/C/D/F/S, Ctrl+Plus, F5, Alt+Enter.
- STRINGTABLE -- 4 "%d item(s)", 5 ", %d Selected", 7 "Select a filename to save",
  8 "Remote Desktop List" (report title), 9 "Select Remote Desktop Filename",
  501-506 export format names, 601 "Remote Desktop Files".

## Command-line switches
Switch strings present in the binary -- `/stext /shtml /sverhtml /sxml /stab /stabular`
(export), `/savelangfile`, `/deleteregkey`, `/noreg`. A target `.rdp` is **not** passed
on the command line. It is opened through File > Open or drag and drop, and the default
source is always the My Documents scan.

**The export switches are vestigial in v1.02.** The CLI export dispatcher `sub_40707E`
always returns 0, so `v14 <= 1 || sub_40707E() == 0` always takes the GUI branch. Running
`rdpv.unpacked.exe /stext out.txt` launches the GUI and writes nothing. This matches the
readme -- "Version 1.02 - Removed the command-line options that export the passwords to a
file from the official version."

## Deviations from the original
- **Unicode (WTL) rather than the original ANSI build.** All recovered data round-trips
  as ASCII/UTF-16, so exports stay faithful.
- **DPAPI linked statically** (crypt32.lib `CryptUnprotectData`) rather than loaded by
  `LoadLibrary`. Behaviourally identical.
- **CLI export re-enabled.** The `/s*` switches are present in the original binary but
  stubbed. The recreation implements them (same NirSoft family behaviour as the sibling
  VNCPassView tool and matching the embedded switch strings), which also enables the
  automated export validation. A `/selftest` crypto round-trip and `/nosaveload` are added.
- **Config stored in the registry key** `HKCU\Software\NirSoft\rdpv` (the confirmed key
  name). The original defaults to an `rdpv.cfg` INI file. `/noreg` and `/deleteregkey` are
  recognized, the `Columns` layout blob uses a small private layout.
- **Tolerant .rdp encoding.** The reader accepts UTF-16LE (with or without BOM, as the
  original assumes) and also ANSI/UTF-8, where the original reads wide only.
- **Full field fidelity** where the original uses fixed 126-char buffers.

## Status
Data and presentation fully recovered and reconstructed as clean C++20 + WTL under
`src/rdpv/`, reusing the extracted resources (`res/rdpv.rc`). **Debug and Release (Win32)
both build clean.**

Validation:
- **Crypto self-check** -- `/selftest` protects a known password with `CryptProtectData`
  and recovers it through the exact DPAPI path, plus a fixed hex-decode vector. Exit code
  **0 (pass)**.
- **Round-trip recovery vs the original** -- a controlled `test.rdp` was built by
  DPAPI-protecting the known password `Secret123!` (246-byte blob, over the 0x20 gate),
  hex-encoding it, and writing `password 51:b:<hex>` plus `full address`, `username` and
  `domain` lines as UTF-16LE. Placed in My Documents and opened by both binaries:
  - The **original** GUI lists it with Password **`Secret123!`**, Computer Name
    `myserver.example.com`, Domain `TESTDOMAIN`, User Name `TESTUSER`.
  - The **rebuild** GUI lists the identical row, and the rebuild `/stext` export prints
    the same recovered values. Both recover the same plaintext from the same file.
- **Export** -- the original's CLI export is disabled (above), so a direct `/s` diff
  against it is not possible. The rebuild `/stext` output is correct and well-formed.
- **GUI** -- the rebuild launches without crashing. Screenshot `analysis/rdpv-gui.png`
  shows the title, File/Edit/View/Help menu, the 6-button toolbar from the extracted
  bitmap, the five columns Computer Name / Domain / User Name / Password / Filename in the
  recovered order, grid lines, and the "2 item(s)" status bar -- matching the original
  window captured side by side. The test file was removed from My Documents afterwards and
  all started processes were killed.

## Fidelity and HRC pass

Config storage (changed). The config routine `sub_40674C` builds the name from
`GetModuleFileNameA`, strips the extension, and appends `.cfg`, so the shipped
`rdpv.exe` writes **`rdpv.cfg`** next to itself. It opens that file as an INI with
section **`[General]`** and reads or writes `ShowGridLines`, `SaveFilterIndex`,
`ShowInfoTip`, `WinPos`, `Columns`, and `Sort`, with binary values encoded as
uppercase space-separated hex (the same `"%2.2X "` framework as VNCPassView). The
registry string `Software\NirSoft\rdpv` and the `/noreg` switch string are both
present but carry zero cross-references, so the registry path is dead in this version.

The recreation had been storing config in that dead registry key. It now derives the
name from the module file the same way (giving `rdpv.cfg`), writes section `[General]`
with the same value names and hex format, and keeps the registry constant only for
`/deleteregkey`.

HRC v1.1 light touch. The new config helpers use static_cast, nullptr, and initialized
locals. Added `[[nodiscard]]` to `FormatFromSwitch`, `HexToBytes`, `RecoverRdpPassword`,
`RecoverRdpPasswordHex`, `dpapi::selfTest`, `ParseRdpFile`, and `EnumerateRdpPasswords`,
whose results or status are load bearing. No cast sweep and no reformatting, following
HRC section 18.

Rebuild and re-validation. All four configs build clean (Debug and Release, Win32 and
x64). A config round-trip against the original confirms both write `rdpv.cfg` with
section `[General]`, matching value names, and a byte-for-byte matching `WinPos`
encoding (default geometry 640 by 480). The toolbar empty area reads 240,240,240. The
`/s` export path was not touched.
