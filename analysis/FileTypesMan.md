# FileTypesMan -- reverse-engineering notes

Target: `targets/filetypesman-x64/FileTypesMan.exe` -- NirSoft FileTypesMan v2.01, x64 PE,
Unicode, not packed. sha256 `c3c83dfa2a38e7ab5fe4e69f85001001c8832aa032ed28a5098bc4679d3ced42`,
md5 `b4cf61985833ae004b4b9b9dabb05d48`. Image base 0x140000000. 465 functions, 276 strings.
Engines used -- IDA idalib session `filetypesman` as the primary decompiler, rea to Ghidra
12.1.2 snapshot `analysis/snapshots/filetypesman.snap` as the cross-check. rea confirmed the
same binary (sha256 match, pe, x86_64) and the function inventory.

FileTypesMan manages the file-type and extension associations held in `HKEY_CLASSES_ROOT`.
The main window has two stacked list panes. The upper pane lists every extension and
registered type, the lower pane lists the shell verbs (actions) of the selected type.

## Entry / startup
- `start` @0x14001bda0, standard msvcrt CRT, then `wWinMain`.
- Command line is parsed with `__wgetmainargs`. `/savelangfile`, `/nosaveload`, the `/s*`
  export switches and `/SelectedExt` are handled before the window is built.
- Main window build in `sub_140010910` (WM_CREATE) -- menu 102, an 8-image toolbar from
  bitmap 104 via `CreateToolbarEx`, a status bar, and two `SysListView32` controls
  (0x103 upper, 0x104 lower). The config is loaded from `FileTypesMan.cfg`.

## Core data model -- the HKCR scan
The driver `sub_14000291C` runs three enumerators in order.

### 1. Extensions, pseudo types and URL protocols -- `sub_1400083FC`
Enumerates every `HKEY_CLASSES_ROOT` subkey. A subkey is taken when it starts with `.`
or equals `folder`, `directory`, `drive` or `*`. When the key is `directory` the extra
pseudo type `Directory\Background` is added. A subkey with no `.` that carries a
`URL Protocol` value is emitted as a URL-protocol record (shown only when the
`ShowURLProtocols` option is on, which is off by default).

### 2. SystemFileAssociations -- `sub_140008170`
Enumerates `HKCR\SystemFileAssociations`. Only subkeys with no `.` at all are emitted
(the perceived-type groupings such as `audio`, `image`, `text`). Their name goes in the
Type Name column and the Extension column stays empty. The `.ext` subkeys are skipped
because the plain HKCR extension row already covers them.

### 3. Applications -- `sub_140007FC8`
Enumerates `HKCR\Applications`, taking each app key that has a `Shell` subkey. The app
key name goes in the Type Name column and its default value in the Description column.

### Per-type reader -- `sub_140007BCC` and the class reader `sub_140007A80`
For an extension the ProgID comes from the extension key default value, resolved through a
`CurVer` redirect. The class fields are read from that ProgID class, or from the extension
key itself when the extension has no ProgID. A Vista-plus UserChoice ProgID is read from
`HKCU\...\Explorer\FileExts\<ext>\UserChoice\Progid`. When a UserChoice is present it
overrides the class fields through `sub_140008A28`, but only the values it actually defines
replace the ProgID values (a missing value leaves the ProgID value in place). The registry
key owner comes from the key security descriptor through `LookupAccountSidW` (`sub_14001886C`),
formatted `domain\name`.

Field sources recovered:

| field | source |
|-------|--------|
| Extension | the HKCR subkey name |
| Type Name | extension default value (ProgID), CurVer resolved |
| Description | class key default value |
| MIME Type | value `Content Type` of the extension key |
| Perceived Type | value `PerceivedType` of the extension key |
| In New Menu | `ShellNew` subkey carries a `NullFile` value |
| Excluded | `EditFlags` bit 0 (FTA_Exclude) |
| Always Show Extension | value `AlwaysShowExt` present on the class |
| Flags | value `EditFlags` on the class (read as 4 raw bytes, REG_DWORD or REG_BINARY) |
| Browser Flags | value `BrowserFlags` on the class |
| Default Icon | default of the class `DefaultIcon` subkey |
| Modified Time | last-write time of the extension key |
| File Type Group | Standard, Perceived Type, URL Protocol or Application |
| User Choice | UserChoice ProgID |
| Icon Handler CLSID | default of the class `ShellEx\IconHandler` subkey |
| Product Name / Version / Description / Company | version info of the Default Icon file |
| Registry Key Owner | account that owns the key |

### Action reader -- `sub_140009578` and `sub_140009230`
For the selected type the actions come from `<class>\shell`. The shell key default value is
the default verb. Each verb subkey yields one record. The command line is read from
`shell\<verb>\command` and resolved to a process filename by `sub_14000902C` (handles
rundll32, quoted paths, `.exe` and `.dll` detection, environment expansion, and a trailing
resolution of a bare filename against the system directory). DDE fields come from the verb
`ddeexec` subkey. A caption that starts with `@file,-id` is replaced by the resource caption
when the `Extract Resource Captions` option is on. Actions are also read from
`SystemFileAssociations\<ext>\shell` for a normal extension.

Action field sources: Name (verb subkey), Caption (verb default value, falling back to the
verb name), Default (verb equals the shell default), Disabled (`LegacyDisable` value present),
Extended (`Extended` value present), Icon (`Icon` value), Last Modified (newest of the verb
and command key times), Registry Key (`HKEY_CLASSES_ROOT\<class>\Shell\<verb>\Command`),
Product columns (version info of the process filename), Registry Key Owner (key owner).

## Upper pane columns (20, table `unk_140023230`, all visible by default)
Field index matches the getter order. Titles from STRINGTABLE 1001..1021.

| # | title | width |
|---|-------|-------|
| 0 | Extension | 110 |
| 1 | Type Name | 130 |
| 2 | Description | 150 |
| 3 | MIME Type | 100 |
| 4 | Perceived Type | 110 |
| 5 | In New Menu | 90 |
| 6 | Excluded | 90 |
| 7 | Always Show Extension | 90 |
| 8 | Flags | 100 |
| 9 | Browser Flags | 100 |
| 10 | Default Icon | 250 |
| 11 | Modified Time | 120 |
| 12 | File Type Group | 90 |
| 13 | User Choice | 90 |
| 14 | Icon Handler CLSID | 150 |
| 15 | Product Name | 150 |
| 16 | Product Version | 120 |
| 17 | Product Description | 150 |
| 18 | Company Name | 120 |
| 19 | Registry Key Owner | 150 |

The In New Menu, Excluded and Always Show Extension columns render `*` when true and empty
when false.

## Lower pane columns (20, table `dword_1400235F0`, all visible by default)
Titles from STRINGTABLE 1101..1120. Field 10 keeps the original STRINGTABLE title and
carries the DDE Topic value.

| # | title | width |
|---|-------|-------|
| 0 | Name | 100 |
| 1 | Caption | 130 |
| 2 | Default | 70 |
| 3 | Disabled | 70 |
| 4 | Last Modified | 120 |
| 5 | Command-Line | 350 |
| 6 | Icon | 250 |
| 7 | DDE Message | 100 |
| 8 | DDE Application | 100 |
| 9 | DDE Ifexec | 100 |
| 10 | Select executable file | 100 |
| 11 | Process Filename | 240 |
| 12 | In SystemFileAssociations | 100 |
| 13 | Registry Key | 350 |
| 14 | Extended | 80 |
| 15 | Product Name | 150 |
| 16 | Product Version | 120 |
| 17 | Product Description | 150 |
| 18 | Company Name | 150 |
| 19 | Registry Key Owner | 150 |

The Default, Disabled, In SystemFileAssociations and Extended columns render `Yes` or `No`.

## Config -- `FileTypesMan.cfg`, section `[General]`
The config is an INI file next to the executable, not a registry key, in the same style as
the other modern NirSoft tools. Written and read by `sub_140001F84` (per-value settings),
`sub_14000C910` (column layout) and `sub_140015148` (window placement). Value names and
defaults recovered:

`ShowGridLines` 0, `SaveFilterIndex` 0, `ShowInfoTip` 1, `DontLoadIcons` 0,
`BlackBackground` 0, `DontLoadHangIcons` 1, `VerSplitLoc` 21844, `MarkDisabledActions` 1,
`AutoDesktopRefresh` 1, `ShowURLProtocols` 0, `ExtractResourceCaption` 1,
`ShowApplicationsTypes` 1, `RemoveExtensionDot` 0, `AddExportHeaderLine` 0,
`MarkOddEvenRows` 0, `NoIconsExt` (default `.appref-ms`), `MainFont` (92-byte LOGFONT),
`SelectedExt` (last selected extension). Plus `WinPos` (window placement),
`Columns` and `Sort` (upper pane), `ActionsColumns` and `ActionsSort` (lower pane).
`/nosaveload` disables load and save.

## Export and command line
Formats and the save-dialog filter order from STRINGTABLE 501..507 -- Text, Tab delimited,
Tabular, HTML Horizontal, HTML Vertical, XML, Comma delimited. The save title is
`Select a filename to save` and the report title is `File Types List`. Output is written in
the system ANSI code page with CRLF line endings and no header line by default.

CLI save-and-quit switches -- `/stext`, `/stab`, `/scomma`, `/stabular`, `/shtml`,
`/sverhtml`, `/sxml`, each followed by a filename (an empty name writes to stdout). `/sort`
takes a column index or title with an optional `~` for descending and may repeat. `/nosort`
saves unsorted. `/SelectedExt <ext>` selects the extension in the upper pane, and combined
with an `/s*` switch it exports the actions of that extension. `/savelangfile` writes the
translation template. Option switches `/DontLoadIcons`, `/MarkDisabledActions`,
`/AutoDesktopRefresh`, `/ShowURLProtocols`, `/ExtractResourceCaption`,
`/ShowApplicationsTypes` each take `0` or `1`.

## Menus, toolbar, dialogs
Main menu 102 with File, Edit, View, Actions, Options and Help. Context menus 104 (types)
and 111 (actions). Accelerators 103. The 8-image toolbar (bitmap 104, 128x16) holds Save,
Refresh, Copy, Properties, Find, two Sort-By buttons and Exit, loaded with
`LoadToolbarImageList` so the strip is masked and renders as icons. Dialog templates
recovered -- Properties 105, Exception 107, Edit File Type 109, Edit Action 110, About 112,
Replace File Type 113, Select Process 114, Send Report 115, Column Settings 1096.

## Recreation
Clean C++20 plus WTL under `src/FileTypesMan/`, reusing the extracted resources
(`res/FileTypesMan.rc`). Builds to `FileTypesMan.exe` for x64 in both Debug and Release.
Layout files -- `FileType.h` and `FileAction.h` (records and getters), `Columns.h` (both
column tables), `FileTypeEnumerator.cpp` (the three HKCR walkers plus the action scan),
`Export.cpp` (the seven formats), `Dialogs.h` (About, Properties, Column Settings, a small
input prompt), `MainFrame.h` (two-pane splitter window, toolbar, status bar, config,
commands) and `FileTypesMan.cpp` (entry point and command line). The list panes are
owner-data, which displays identically to the original and scales to the full key set.

## Validation
The original and the rebuild were run with `/scomma` and the outputs compared row by row.

- Types list -- 1212 rows from each, a 100 percent identity match (zero rows missing, zero
  extra). 1182 of 1212 rows match on every column (97.5 percent). The 30 rows that differ
  fall in two columns only.
  - MIME Type, 8 rows. All are Office Open XML types whose value is longer than the
    original's 63-character field, so the original drops them. The recreation keeps the full
    value, the fixed-buffer-truncation improvement the guide allows.
  - Product columns, 22 rows. These are file-type icons whose version info the original does
    not load (mostly the Windows Compressed Folder handler `zipfldr.dll`). The recreation
    reads version info from every resolved Default Icon file.
- Actions list -- `/SelectedExt .txt`, `.html` and `.reg` exported from both tools are
  byte-identical, including the multi-verb `.reg` type (edit, open with the `Mer&ge` caption,
  preview and print) with matching commands, process filenames, registry keys and owners.

The GUI was launched and the top strip captured to `analysis/FileTypesMan-gui.png`. The
toolbar renders its icons rather than a dark box, the menu and both panes are in place, and
the upper pane shows the recovered columns in the recovered order.

## Deviations
- The config store is `FileTypesMan.cfg` rather than a `HKCU\Software\NirSoft` key. v2.01
  uses the INI file, matching the other modern tools. The recovered value names are listed
  above.
- The MIME Type column keeps full values where the original truncates at 63 characters.
- The list panes are owner-data where the original inserts items. The display is identical.
- The build uses the v145 toolset (`14.51.36231`) because the v143 default toolset on this
  machine no longer has ATL installed. The project keeps `v143` like its siblings and builds
  with `/p:PlatformToolset=v145`.
- Writes implemented -- New File Extension, Delete Selected Extensions, Delete Selected
  Actions and the RegEdit jumps. The Edit File Type and Edit Action write dialogs, Replace
  File Type, the Send Report over sockets to extension.nirsoft.net, Run As TrustedInstaller
  and the font and icon pickers are present in the menu but not wired in this build.

## Status
Data model and presentation fully recovered and validated. Both panes reproduced. Debug and
Release x64 built. The extension enumeration matches the original one for one across 1212
rows and the action enumeration matches byte for byte on the extensions tested.

## Fidelity and HRC pass

Config storage. The original persists to FileTypesMan.cfg next to the executable, section
[General], through GetPrivateProfileString and WritePrivateProfileString. There is no
Software\NirSoft config key. The only Software paths in the binary serve HKCR data and the
regedit jump, not settings. The recreation already stores its config in FileTypesMan.cfg
[General] with the recovered value names, so it matched and was left unchanged.

HRC v1.1 light touch. Already clean. No bare NULL, and the column kind field is a plain
LVCFMT integer rather than an unscoped enum, so no enum class change applied. No source edit
was needed.

Rebuild and re-validation. Debug and Release x64 rebuilt clean with the v143 toolset. The
ToolbarWindow32 empty area reads 240,240,240. The source is unchanged, so the earlier 1212
row /scomma match and the geometry result still hold.

## MainFrame split into header and source

MainFrame.h carried the whole CMainFrame window class with every method body inline, 851
lines. The bodies moved out to a new MainFrame.cpp, leaving the header with the class shape
only: the members, the DECLARE_FRAME_WND_CLASS macro, the message map, the AppOptions struct
and the method declarations. The header is now 217 lines. MainFrame.cpp is 1114 lines after
the HRC clang-format pass, 33350 bytes. This is behaviour-preserving code motion in the WinObj
pattern. MainFrame.cpp was added to the project ClCompile group. The out-of-line definitions
drop override and static and keep const, in the same order as the header.

Re-validation after the split:

- Both x64 configs rebuilt clean with the VS 2022 Community MSBuild at the v143 toolset.
- The /scomma export of the full type list is byte-identical between the pre-split and the
  post-split Release binary. Each writes 1212 rows with sha256
  `9DF070F335C49BA14FF0D0F78D049F9301A19767D729AE3C355A1A6DE75BD3EB`. The export path in
  FileTypesMan.cpp calls no CMainFrame method, so the move cannot reach it, and the byte match
  confirms that. The pre-split binary was built from the original 851-line header in an
  isolated copy of the working tree.
- The elevated GUI, launched read-only with /nosaveload through the broker, loads the upper
  file-type list with 1212 rows, the same count as the export. The lower actions pane is empty
  until a type is picked, as in the original.
- Window geometry is 640x480, matching the original CreateWindowEx default with the config
  cleared.
- The ToolbarWindow32 empty area reads 240,240,240 under elevation, so the light-toolbar fix
  holds after the split. The band renders its 8 icons rather than a dark box. An elevated
  helper routed the capture, since a medium integrity session cannot PrintWindow an elevated
  window.
