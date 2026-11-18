# Active Directory Explorer

Source-accurate recreation of Sysinternals **Active Directory Explorer** (ADExplorer),
focused on the browse and read path. Reconstructed from
`targets/sysinternals/ADExplorer64.exe` (x64, version 1.52, Mark Russinovich).
Project at `src/ADExplorer/`.

## What it is

ADExplorer connects to an Active Directory or LDAP server and browses it. A connect
dialog takes a server or domain name and an optional user and password. The main window
has a tree on the left of the directory hierarchy, rooted at the naming contexts from the
rootDSE, and an attribute list on the right for the selected object with the columns
Attribute, Syntax, Count and Value. A search dialog runs an LDAP filter. The full tool
also edits attributes, saves and compares snapshots, and creates and deletes objects,
all of which are out of scope here (see below).

## Key reverse-engineering findings

### Browse and read use ADSI, not raw ldap_search

The most important finding corrects the brief. The brief assumed the browse and read path
runs on `wldap32` (`ldap_search_ext_s` and `ldap_get_values`). The binary does not import
`ldap_search` at all. Its WLDAP32 imports are only `ldap_initW`, `ldap_connect`,
`ldap_set_optionW`, `ldap_bind_sW`, `ldap_modify_ext_sW`, `ldap_rename_ext_sW`,
`ldap_err2stringW` and `ldap_unbind`. Browse and read go through **ADSI** instead
(`ACTIVEDS`: `ADsOpenObject`, `IDirectorySearch`, `ADsBuildVarArrayStr`, `FreeADsMem`,
`ADsGetLastError`). WLDAP32 is reserved for the write path (modify and rename). This
matches the repo's own `AdRestore` recreation, which also binds through ADSI and writes
through `ldap_modify_ext_sW`. The two routes reach the same LDAP protocol operations, a
BASE read of the rootDSE, a ONELEVEL enumerate of a container, and a BASE read of an
object's attributes, so the recreation reproduces that exact call sequence through ADSI.

`ADsOpenObject` is called at many sites. The small wrapper `sub_1400067A0` binds a
container with flags `0x21` (`ADS_SECURE_AUTHENTICATION | ADS_FAST_BIND`) passing the
stored user and password. `sub_1400104D0` composes an `LDAP://<server>/schema/...` path
and binds the schema with flag `0x1` (`ADS_SECURE_AUTHENTICATION`).

### EULA and settings keys

From `sub_140001F90` (the EULA check):

- Acceptance is read, in order, from `HKLM\Software\Sysinternals`,
  `HKCU\Software\Sysinternals` and `HKCU\Software\Sysinternals\Active Directory Explorer`,
  value `EulaAccepted` (REG_DWORD), opened with `KEY_QUERY_VALUE | KEY_WOW64_64KEY`.
- The per-tool subkey is **`Active Directory Explorer`**, not `AD Explorer` as the brief
  guessed. The recreation records consent under
  `HKCU\Software\Sysinternals\Active Directory Explorer\EulaAccepted`.

Window placement and settings live under `HKCU\Software\MSDART\Active Directory Explorer`
(created in `WinMain` at `0x140022540`). The recreation keeps placement and splitter width
in an `ADExplorer.ini` beside the exe, matching the NirSoft tools in this repo.

### Window and command structure (WinMain 0x140022540)

- Command line usage string, read from the binary:
  `adexplorer [-snapshot <connection-string> <snapshot-file>] [-noconnectprompt]`.
  The recreation honours `-noconnectprompt` and `-accepteula`, and accepts `-snapshot`
  while reporting that snapshots are out of scope.
- The main window is a modeless `MAIN` dialog created with `CreateDialogParamW`, template
  `MAIN` (`237 x 173` dialog units, `DS_CENTER`, resizable), dialog proc `sub_14001B780`.
  Its window class is `Active Directory Explorer Main`, menu `MENU_MAIN`.
- A second window class `WinSplitterClass` (`sub_14001A810`) is the tree and list splitter.
- The toolbar and status bar are created in the dialog proc, not in WinMain.

### Toolbar (CreateToolbarEx at 0x14001dbd7)

The toolbar uses bitmap resource `400` (a `192 x 15`, 4-bit strip of 12 images at 16 px) and
the `TBBUTTON` array at `0x140084B20`. Decoded buttons and commands:

| Image | Command id | Action |
|------:|-----------:|--------|
| 6 | 40072 | Connect |
| 3 | 117   | Search Container |
| 5 | 40058 | Compare Snapshot |
| 4 | 40073 | Create Snapshot |
| 1 | 40057 | Properties |
| 9 | 40077 | Back |
| 10 | 40076 | Forward |
| 11 | 40078 | History |

Back, Forward and History start disabled. The recreation reproduces this layout and the
light-mode toolbar fix from the guide.

### Source map (ADExplorer64.exe)

| Address | Role |
|---------|------|
| `0x140022540` | `WinMain`, MSDART settings key, arg parse, register classes, run the MAIN dialog |
| `0x14001B780` | main dialog proc, builds toolbar and status bar, hosts the tree and list |
| `0x14001A810` | `WinSplitterClass` window proc |
| `0x140001F90` | EULA check against the three registry locations |
| `0x140001820` | EULA write |
| `0x1400067A0` | `ADsOpenObject` container bind wrapper, flags `0x21` |
| `0x1400104D0` | schema bind, composes `LDAP://<server>/schema/` |
| `0x1400367E0` | write path, binds with `ldap_bind_sW` and calls `ldap_modify_ext_sW` |

## Core reproduced (the deliverable)

- **Connect dialog and connect logic.** `CConnectDlg` takes a server or domain, an optional
  user and password. `ad::Client::Connect` binds `LDAP://[server/]RootDSE` through
  `ADsOpenObject` and reads `defaultNamingContext`, `configurationNamingContext`,
  `schemaNamingContext` and the `namingContexts` list, matching a BASE read of the rootDSE.
- **Tree browse.** Expanding a node runs `ad::Client::EnumerateChildren`, an
  `IDirectorySearch` with scope `ADS_SCOPE_ONELEVEL` and filter `(objectClass=*)`, adding
  each child as a tree item labelled with its RDN. Expansion is lazy because a directory is
  large.
- **Attribute list.** Selecting an object runs `ad::Client::ReadAttributes`, an
  `IDirectorySearch` with scope `ADS_SCOPE_BASE` and all attributes, filling the columns
  Attribute, Syntax, Count and Value. The common syntaxes are formatted for display: DN
  string, unicode and other strings, integer and large integer, boolean as TRUE or FALSE,
  UTC time as local date and time, octet string as hex, `objectSid` as `S-1-...` through
  `ConvertSidToStringSidW`, and `objectGUID` as a GUID through `StringFromGUID2`.
- **Search dialog.** `CSearchDlg` takes a base DN, a raw LDAP filter and a scope (Subtree,
  One Level or Base), runs `ad::Client::Search`, and lists matching distinguished names.
  Double-clicking a result loads that object's attributes.

## Out of scope (reproduced for fidelity, body stubbed)

The menu, toolbar and some dialog resources for these are present for fidelity, but the
logic is not implemented. Each shows a short note when invoked.

- Snapshot save, snapshot compare and the snapshot file format (`File, Create Snapshot`,
  `Compare`, the `-snapshot` switch).
- Create and delete of objects.
- The modify history (Back, Forward, History).
- Favorites.
- Attribute **modify**. The write path is reproduced for fidelity as
  `ad::Client::ModifyAttribute`, which follows the original `sub_1400367E0`
  (`ldap_initW`, `ldap_set_optionW`, `ldap_connect`, `ldap_bind_sW`, then
  `ldap_modify_ext_sW`). Per the safety rule it is never wired to the GUI and is never
  exercised, so no directory object is ever changed by this recreation.
- Rename (`ldap_rename_ext_sW` in the original).

## Validation

No Active Directory or LDAP server was available on this workstation, and per the security
rule none was contacted. No live LDAP target was used. Validation was by inspection against
the decompile plus a live check of the GUI shell.

- **LDAP call sequence by inspection.** Connect reads the rootDSE naming contexts, browse
  runs a ONELEVEL `(objectClass=*)` search, attribute read runs a BASE all-attributes
  search, and search runs the user filter at the chosen scope. This matches the original's
  ADSI sequence.
- **Build.** MSBuild v143, Debug and Release, both x64, both clean. Release carries the
  repo-wide hardening from `Directory.Build.targets` and the static CRT.
- **Manifest.** The embedded manifest is `asInvoker` with the Common-Controls v6 dependency,
  byte-for-level identical to the original (checked with `mt.exe`).
- **GUI shell.** The window renders with the File, Edit, Favorites, Search, Compare, History
  and Help menus, the light toolbar, the path band, the tree and attribute-list splitter and
  the status bar. The `ToolbarWindow32` empty area reads `240, 240, 240`.
- **Geometry.** First launch is `372 x 340` pixels, matching the original MAIN template of
  `237 x 173` dialog units centered on the work area. Saved placement overrides on later runs.
- **Connect dialog.** Renders `Connect to Active Directory` with the server, user and
  password fields and the snapshot and save controls, matching the original CONNECT template.
- **About credit.** Renders `Independent source-accurate recreation of AD Explorer by
  Sysinternals, through reverse engineering and rewritten in modern C++20 & WTL 10.` and
  `Author: 0xntpower`.
- **EULA.** `-accepteula` records `HKCU\Software\Sysinternals\Active Directory
  Explorer\EulaAccepted = 1`, verified in the registry.

## Refactor: window-class bodies split into .cpp units (2026-11-18)

Behaviour-preserving code motion, matching the WinObj pilot (commit 4a3adc0).

- `MainFrame.h` now carries only the class shape, the member variables, the message map,
  `DECLARE_FRAME_WND_CLASS` and the method declarations. Every `CMainFrame` method body
  moved verbatim to the new `MainFrame.cpp` in the same order, dropping `override` and
  `static` on the out-of-line definitions while the declarations keep them. The header
  went from 634 to 149 lines, and `MainFrame.cpp` is 594 lines.
- `AdClient.h` is a class with inline method bodies, so it got the same treatment. The
  non-template `ad::Client` members moved to the new `AdClient.cpp` (299 lines), while the
  structs, the inline free helpers in `ad::detail`, the trivial accessors and the template
  `Bind` stay inline in the header. The header went from 615 to 311 lines. The header no
  longer defines `WIN32_LEAN_AND_MEAN`, so a primary unit that includes it first still
  binds the full OLE surface `activeds.h` depends on, matching the AdRestore recreation.
- `MainFrame.cpp` and `AdClient.cpp` were added to `ADExplorer.vcxproj`. The thin entry
  `ADExplorer.cpp` is unchanged apart from the HRC clang-format pass over `src/ADExplorer`.
- Both x64 configurations rebuild clean with MSBuild v143 after the move.

### Re-validation after the split

No Active Directory or LDAP server was contacted. The rebuilt Release binary was launched
read-only, the connect fields left blank, and nothing was connected or modified.

- The main window renders at `372 x 340` pixels with the config cleared, matching the
  first-launch geometry. The child controls enumerate as `ToolbarWindow32`,
  `msctls_statusbar32`, the path `Edit`, `SysTreeView32`, `SysListView32` and `SysHeader32`,
  so the toolbar, status bar, path band, tree and attribute list all build.
- The `ToolbarWindow32` child, found with `FindWindowExW(frame, 0, "ToolbarWindow32", null)`,
  reads `240, 240, 240` across its empty area, so the light-mode toolbar fix survives the move.
- The connect dialog renders with the title `Connect to Active Directory` and the server,
  user and password fields, left empty.
- The `ldap_modify` write path (`ad::Client::ModifyAttribute`) stays unwired and is never
  called from the GUI, so no directory object can be changed.

## Security note

Directory contents are sensitive. This recreation browses and reads only, never modifies,
creates or deletes, and was validated without touching any real directory.
