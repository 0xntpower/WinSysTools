# VNCPassView — reverse-engineering notes

Target: `targets/vncpassview/VNCPassView.exe` — NirSoft VNCPassView v1.05, x86 PE, **ANSI/MultiByte**, VS2005-era (`c:\Projects\VS2005\VNCPassView\Release\VNCPassView.pdb`).
sha256 `816d7616238958dfe0bb811a063eb3102efd82eff14408f5cab4cb5258bfd019`, md5 `d28f0cfae377553fcb85918c29f4889b`. Image base 0x400000.
298 functions (IDA), 291 strings. Engines used — IDA idalib session `vncpassview`, cross-checked with rea to Ghidra 12.1.2 snapshot `analysis/snapshots/vncpassview.snap`.

Not packed. Uses the larger NirSoft shared framework (owner-generated column manager, language-file string loader `sub_40391D`, exception dialog, HTML/XML/CSV export engine) rather than the tiny MIMEView-era shell.

## Entry / startup
- `start` @0x408ae8 — VS2005 CRT, then WinMain.
- The main window is a framework object. WM_CREATE is `sub_406185` — builds menu 102, a status bar (`CreateStatusWindowA`, id 0x101), a 5-button toolbar (`CreateToolbarEx`, id 0x102, bitmap 0x68), and a `SysListView32` report list (id 0x103). Columns come from a descriptor table walked into the list through `sub_401BFB`.
- The password scan is a virtual method `sub_401911` (vtable slot 7 of the main class at 0x4094a0), which calls `sub_40867A(this+468)`.

## Core data model — the VNC sources
The scan `sub_40867A` reads a fixed set of server keys. For each it opens the key (`sub_40801F`, `RegOpenKeyExA` with samDesired 0x20019 = KEY_READ), reads the REG_BINARY **`Password`** value (`sub_4089B4`, capped under 255 bytes), and hands the ciphertext plus two codes to the decrypt-and-add routine `sub_4088EF`.

| # | Hive | Registry key | hive code | version code |
|---|------|--------------|-----------|--------------|
| 1 | HKCU | `SOFTWARE\RealVNC\WinVNC4`        | 2 Current User | 4 |
| 2 | HKLM | `SOFTWARE\RealVNC\WinVNC4`        | 1 System       | 4 |
| 3 | HKCU | `SOFTWARE\ORL\WinVNC3`            | 2 Current User | 3 |
| 4 | HKCU | `SOFTWARE\ORL\WinVNC3\Default`    | 2 Current User | 3 |
| 5 | HKLM | `SOFTWARE\ORL\WinVNC3`            | 1 System       | 3 |
| 6 | HKLM | `SOFTWARE\ORL\WinVNC3\Default`    | 1 System       | 3 |
| 7 | HKCU | `SOFTWARE\TightVNC\Server`        | 2 Current User | 0 |
| 8 | HKLM | `SOFTWARE\TightVNC\Server`        | 1 System       | 0 |

Then `sub_4087BF` handles **UltraVNC**: `sub_40806F` opens `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Ultravnc2_is1` and reads `InstallLocation`. The install path plus `ultravnc.ini` is read with `GetPrivateProfileStringA(section "ultravnc", key "passwd")`. That value is a hex string, parsed to bytes by `sub_403382` (two hex chars per byte, spaces skipped), then decrypted the same way with hive code 1 System and version code 1.

## The fixed DES key and the decrypt routine
`sub_4088EF` writes the key one byte at a time onto the stack, then runs d3des over each 8-byte block of the stored blob:

| offset | byte |
|--------|------|
| 0 | 0x17 |
| 1 | 0x52 |
| 2 | 0x6B |
| 3 | 0x06 |
| 4 | 0x23 |
| 5 | 0x4E |
| 6 | 0x58 |
| 7 | 0x07 |

That is the fixed, well-known VNC obfuscation key (decimal 23, 82, 107, 6, 35, 78, 88, 7). It is never stored contiguously in the file, which is why a raw byte search misses it (it is built with eight immediate `mov` instructions). **rea to Ghidra independently confirms the same stack writes** (`local_11c=0x17 .. 0x52 .. 0x6b .. 0x23 .. 0x4e .. 0x58`, with the 0x06 and 0x07 bytes in the gaps).

- `sub_4020EB` is d3des `deskey`, pre-configured for **decryption**. Its round loop stores subkeys in reverse order (`v15=120, v16=124` decreasing). `sub_4021F6` is `cookey`.
- `sub_40229A` is the d3des `des` block function (scrunch, `sub_402315` = desfunc, unscrun).
- Per block it DES-decrypts the ciphertext under the fixed key and writes plaintext. The result is NUL-terminated at `len/8`. VNC passwords are ASCII and capped at 8 characters.

VNC stores a password by DES-**encrypting** the plaintext with this key, so recovery is a straight DES-**decrypt**. The reconstruction implements a clean standard DES rather than transliterating the d3des tables, which is behaviourally identical for ECB.

## Record struct (276 bytes, 0x114) — built by `sub_4018A7`
The raw 264-byte struct from `sub_4088EF` is copied into a record object (vtable `off_40944C`):

| offset | field |
|--------|-------|
| +0x00  | vtable |
| +0x0C  | password string (decrypted, 256 bytes) |
| +0x10C | hive code (1 System, 2 Current User) |
| +0x110 | version code (0 TightVNC, 1 UltraVNC, 3 VNC3, 4 RealVNC4) |

## Columns — descriptor table at 0x40b990, getter at 0x401821
Display order is taken from the descriptor table (title ids 1001, 1003, 1002 in that order). The record getter `sub_401821` maps the display column index to the field:

| # | Title (STRINGTABLE) | source | value |
|---|---------------------|--------|-------|
| 0 | Password Type (1001) | hive code | module string 1100 + hive  ->  1101 "System" / 1102 "Current User" |
| 1 | Version (1003)       | version code | `itoa(code)` base 10, and **blank when the code is 0** (TightVNC) |
| 2 | Password (1002)      | password | the decrypted string |

So Version shows "4" for RealVNC4, "3" for VNC3, "1" for UltraVNC, and nothing for TightVNC. This quirk is reproduced exactly.

## Config — `HKCU\Software\NirSoft\VNCPassView`
Values `WinPos` (WINDOWPLACEMENT), `Sort` (DWORD sort column), `Columns` (column-manager blob), `ShowGridLines` (DWORD), `SaveFilterIndex` (DWORD), `ShowInfoTip` (DWORD). `/nosaveload` skips both load and save. `/deleteregkey` removes the key.

## Commands (menu 102 / context 104 / accel 103)
Save(40001) Exit(40002) About(40003) Refresh(40004, F5) ShowGridLines(40005) HtmlReportAll(40006) SelectAll(40007, Ctrl+A) Copy(40008, Ctrl+C) Properties(40009, Alt+Enter) HtmlReportSelected(40010) ChooseColumns(40011) AutoSizeColumns(40012, Ctrl+Plus) Find(40014, Ctrl+F) DeselectAll(40015, Ctrl+D). Toolbar buttons Save/Refresh/Copy/Properties/Exit.

## Export / CLI
Formats (STRINGTABLE 501-506) — Txt, Tab delimited, Tabular, HTML Horizontal, HTML Vertical, XML. Report title "VNC Passwords" (string 8). Save title "Select a filename to save" (string 7).
CLI save-and-quit switches — `/stext /stab /stabular /shtml /sverhtml /sxml <file>`, plus `/nosaveload`, `/deleteregkey`, `/savelangfile`.
XML wrapper matches the original byte for byte — declaration `<?xml version="1.0"  encoding="ISO-8859-1" ?>` (note the double space) and a root element `vnc_passwords` with per-item `<item>` and lowercased underscore tags.

## Deviations from the original
- Unicode (WTL) rather than the original ANSI build. All recovered data is ASCII, so exports stay byte-identical.
- The `Columns` config blob uses a small private layout rather than the original framework blob (only the value name is preserved).
- `/savelangfile` is recognised as a known switch but language-file support is not reconstructed (no behaviour depends on it for password recovery).
- Full record fidelity is kept where the original uses fixed buffers.

## Status
Data and presentation fully recovered and reconstructed as clean C++20 + WTL under `src/VNCPassView/`, reusing the extracted resources (`res/VNCPassView.rc`).

Validation:
- **Crypto self-check** — `/selftest` runs the DES core against the FIPS known-answer vector (key 0x133457799BBCDFF1, plaintext 0x0123456789ABCDEF, cipher 0x85E813540F0AB405) and a VNC round trip with the fixed key. Exit code 0 (pass) in both Debug and Release.
- **`/s` parity** — original and rebuild run to files. `/stext` and `/stab` both produce empty output on this box (no VNC installed) — matching. `/sxml` is byte-identical at 82 bytes.
- **GUI** — the rebuild launches without crashing. Screenshot `analysis/vncpassview-gui.png` shows the title, File/Edit/View/Help menu, the 5-button toolbar from the extracted bitmap, the three columns Password Type / Version / Password in the recovered order, grid lines, and the "0 item(s)" status bar. Both Debug and Release built (Win32).

## Fidelity and HRC pass

Config storage (changed). Re-reading the original settled where it persists. The config routine `sub_4067A2` builds a path from the module directory and appends the hardcoded name **`BasicProg.cfg`** (a leftover from the NirSoft project skeleton, not `VNCPassView.cfg`), opens it as an INI with section **`[General]`**, and reads or writes the values `ShowGridLines`, `SaveFilterIndex`, `ShowInfoTip`, `WinPos`, `Columns`, and `Sort`. Binary values run through `sub_403307`, which formats each byte with `"%2.2X "`, so the stored form is uppercase space-separated hex with no trailing space. The registry string `Software\NirSoft\VNCPassView` is still in the binary but has zero cross-references, so it is dead.

The recreation had been using that dead registry key for its config. It now writes `BasicProg.cfg` next to the exe, section `[General]`, with the same value names and the same hex encoding. The legacy registry constant is kept only for the `/deleteregkey` switch, matching the vestigial key in the original.

HRC v1.1 light touch. The new config helpers use static_cast, nullptr, and initialized locals. Added `[[nodiscard]]` to `FormatFromSwitch`, `EnumerateVncPasswords`, `des::vncDecrypt`, and `des::selfTest`, whose results are load bearing. No reformatting and no cast sweep, following HRC section 18.

Rebuild and re-validation. All four configs build clean (Debug and Release, Win32 and x64). A config round-trip against the original, both run in isolated folders and closed cleanly, confirms both write `BasicProg.cfg` with section `[General]`, matching value names, and a byte-for-byte matching `WinPos` encoding. The `/s` export is unchanged and stays byte-identical to the original (`/sxml` 82 bytes identical, the tab and delimited outputs empty on a box with no VNC installed, matching). The toolbar empty area reads 240,240,240 and the default geometry of 400 by 200 matches.
