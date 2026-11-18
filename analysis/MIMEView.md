# MIMEView — reverse-engineering notes

Target: `targets/mimeview/MIMEView.exe` — NirSoft MIMEView, x86 PE, **ANSI/MultiByte**, VC6-era.
sha256 `92c1320552bc27cedce33e146a25a8467bbd8b2f92ca339b7e71f19358575a7c`. Image base 0x400000.
233 functions (IDA), 227 strings. Engines used — rea to Ghidra 12.1.2 snapshot `analysis/snapshots/mimeview.snap`, plus IDA idalib session `mimeview`.

## Entry / startup
- `start` @0x405776 — standard VC6 CRT (`__set_app_type(_crt_gui_app)`, `_initterm`, `__getmainargs`) then `WinMain@16` @0x404d3a.
- `WinMain`:
  - `sub_404BA3(ctx)` init app context struct (~0x1B0 bytes on stack, `Param`).
  - `InitCommonControlsEx`-wrapper `sub_401733({dwSize=8, ICC=0xFF})`. On fail, MessageBox "Error: Cannot load the common control classes." (`@0x407720`), return 1.
  - `sub_402767(hInstance)` stores hInstance global.
  - `GetVersionExA` to global `VersionInformation` @0x407d38.
  - `sub_4054E8()` early init (settings / OS detect).
  - `sub_403262(lpCmdLine)` command-line parse. `dword_407DCC` holds arg count. If `>1 && sub_404C26()`, exit 0 (CLI save-and-quit path).
  - `LoadAcceleratorsA(hInst, 0x67)` to `hAccTable` @0x407d30.
  - `sub_4010E9()`, then `sub_4010FB()` returns 0 sets `dword_40831C=1` (capability flag).
  - Window build via `sub_404B43(ctx)`, `sub_404B7A(ctx)`, `sub_4046A8(ctx)` (creates main window to `hWnd`).
  - Msg loop — `GetMessageA` plus `TranslateAcceleratorA(hWnd,hAccTable)` plus modeless find-dialog `IsDialogMessageA(sub_4024ED())`.

## Core data model — the MIME record (1108 bytes / 0x454)
Built by **`sub_40391E(ctx)`**, the central routine:
```
open HKEY_CLASSES_ROOT\MIME\Database\Content Type   (sub_4013FA, KEY_READ 0x20019)
for each subkey <contentType> (RegEnumKeyEx wrapper sub_40531A):
    open subkey
    rec = zeroed 1105B
    sub_402A98(rec, <contentType>, 64)              -> +0   MIME type name
    RegQueryValueEx "Extension" -> rec+65  (16B)    -> +65  file extension, e.g. ".pdf"
    RegQueryValueEx "CLSID"     -> rec+408 (64B)    -> +408 CLSID string
    sub_40150B(rec)   CLSID-key enrichment
    sub_401614(rec)   extension/ProgID enrichment
    sub_4038DB(ctx, rec)  append rec to dynamic array (ctx+32=ptr, ctx+36=count, elem 1108)
```

### Record field map (offset : field : source)
| off  | field               | source |
|------|---------------------|--------|
| +0   | MIME content type   | subkey name under `MIME\Database\Content Type` |
| +65  | Extension           | value `Extension` of the content-type key |
| +82  | CLSID class name    | default value of `HKCR\CLSID\<clsid>` |
| +147 | InProcServer32 DLL  | default of `HKCR\CLSID\<clsid>\InProcServer32` (260B) |
| +408 | CLSID               | value `CLSID` of the content-type key |
| +473 | ProgID              | default of `HKCR\CLSID\<clsid>\ProgID` (32B) |
| +506 | Version             | default of `HKCR\CLSID\<clsid>\Version` (32B) |
| +539 | shell open command  | from ext to ProgID key `shell\open\command` default (280B) |
| +820 | DefaultIcon         | from ext to ProgID key `DefaultIcon` default (280B) |
| +1104| flag (DWORD)        | default 1, set 0 when `EditFlags` present AND bit 0x10000 set |

- `sub_40150B(rec)` opens `HKCR\CLSID\<rec.CLSID>` (`sub_4014B9`, via `sprintf("CLSID\\%s")`), reads InProcServer32 / Version / ProgID / default name.
- `sub_401614(rec)` — `sub_401415` opens `HKCR\<rec.Extension>`, reads its default (ProgID), opens `HKCR\<ProgID>`, reads `EditFlags` (to flag), `shell\open\command`, `DefaultIcon`.

### Registry helper layer (generic, reused across NirSoft tools)
- `sub_4050F6` is RegOpenKeyEx. `sub_405136/40515C/405180` are RegQueryValueEx (dword / string / other).
  `sub_40531A` to `sub_40519D` is RegEnumKeyEx. `sub_4052D0` opens subkey and reads a named value. `sub_40526D` is RegQueryValueEx by name.
- Config root `Software\NirSoft\MIMEView` (@0x407764) via RegCreateKeyEx / RegSetValueEx / RegQueryValueEx.

## UI (to confirm when building)
- Main window — ListView (report) plus toolbar (`CreateToolbarEx`) plus status bar (`CreateStatusWindowA`) in `sub_4046A8` / `sub_404B43` / `sub_404B7A`.
- Menu / accels / dialogs in `.rsrc` (accel id 0x67). Export via `GetSaveFileNameA` (txt/csv/html/xml). Find via `FindTextA` (modeless, `sub_4024ED`). Icons via `ExtractIconExA`. Clipboard copy. Properties dialog `DialogBoxParamA` (`DialogFunc`).
- Columns and listview fill live in the window-build cluster. TODO — pull column titles and the field-to-column map from the column-init and ShowRecords functions.

## Columns (sub_4016AC field map + titles, default width 120, stored col8..col0)
| # | Title          | record field | offset |
|---|----------------|--------------|--------|
| 0 | Content Type   | MIME type    | +0    |
| 1 | Extension      | extension    | +65   |
| 2 | Plugin Name    | CLSID default name | +82 |
| 3 | Plugin Version | CLSID\Version| +506  |
| 4 | Plugin ProgID  | CLSID\ProgID | +473  |
| 5 | Plugin File    | InProcServer32 | +147 |
| 6 | Plugin CLSID   | CLSID        | +408  |
| 7 | File Dialog    | flag Yes/No  | +1104 |
| 8 | Open Command   | shell\open\command | +539 |

DefaultIcon (+820) is used only for the per-row small icon (ExtractIcon), not a column.

## Config — `HKCU\Software\NirSoft\MIMEView` (sub_40545B)
`WinPos` (WINDOWPLACEMENT 44B), `Columns` (column-manager blob), `Sort1` (DWORD sort col), `ShowGridLines` (DWORD, default 1). `/noloadsettings` deletes the key first.

## Commands (menu 102 / context 104 / accel 103), IDs 40001..40029
Save(40001) Exit(40002) About(40003) Refresh(40004) EnableFileDialog(40006) SelectAll(40007) Copy(40008) Properties(40009) DisableFileDialog(40010) Find(40011) ShowGridLines(40012) ChooseColumns(40013) AutoSizeCols(40015) HtmlHorizontal(40028) HtmlVertical(40029).

## Export / CLI
Formats — Txt, Tab delimited, Tabular, HTML Horizontal, HTML Vertical. Save title "Select a filename to save". HTML report title "Internet Explorer MIME Types".
CLI save-and-quit switches — `/stext /stab /stabular /shtml /sverhtml <file>`, plus `/noloadsettings`. Toolbar buttons Save/Refresh/Copy/Properties (bitmap 104). Status "%d item(s)" + ", %d Selected".
Enable/Disable File Dialog — flips the EditFlags "file dialog" bit of selected types (prompts "Do you want to enable/disable the file dialog in the selected MIME types ?").

## Status
Data + presentation fully recovered. Reconstructing as clean C++20 + WTL, reusing extracted resources (res/MIMEView.rc).

## Fidelity and HRC pass

Config storage (already correct). The original keeps settings in the registry under `HKCU\Software\NirSoft\MIMEView`, written with RegCreateKeyEx and RegSetValueEx (value names WinPos, Columns, Sort1, ShowGridLines). A string scan of the binary finds that key and finds no `.cfg` name, which fits the VC6-era store from before the NirSoft move to a per-exe `.cfg`. The recreation already uses the same registry key with the same value names, so config storage was left unchanged.

HRC v1.1 light touch. Added `[[nodiscard]]` to the two free functions whose result is load bearing, `EnumerateMimeTypes` and `FormatFromSwitch`. Nothing else changed. The code already honours the cheap rules (nullptr, initialized variables, enum class, static_cast in new code). The wide `(int)container.size()` narrowing casts were left as they are, to match the shared pattern across all tools and avoid churn, following HRC section 18.

Rebuild and re-validation. All four configs build clean (Debug and Release, Win32 and x64) under VS 2022 v143. The rebuild launches without elevation. Its toolbar empty area reads 255,255,255, the classic light look that MIMEView keeps because it carries no comctl32 v6 manifest. Geometry and the `/s` paths were not touched and keep their earlier validated behaviour.
