# CredentialsFileView -- reverse-engineering notes

Target: `targets/credentialsfileview-x64/CredentialsFileView.exe` -- NirSoft CredentialsFileView v1.12, x64 PE, Unicode.
sha256 `35296e7a34688ca3e3159bcdf92b4d60ba4173a2369aca531bb7bc959f68ed9c`, md5 `a3a0e15ee3ea1dfa703ae53dae92f722`. Image base 0x140000000.
849 functions, 301 strings. Engine used -- IDA idalib session on the binary, cross-read against public DPAPI credential-file layout.

## What it does

It reads Windows Credential Manager credential files, the DPAPI-encrypted blobs under
`%LOCALAPPDATA%\Microsoft\Credentials`, `%APPDATA%\Microsoft\Credentials`, and the system
profile, decrypts each with CryptUnprotectData, and shows one row per entry with the
decrypted fields. An upper list pane holds the entries and a lower pane shows the selected
entry's decrypted bytes as a hex dump or a strings list.

## Entry / startup

- `start` @0x140001240 is the standard msvcrt CRT stub, then `wWinMain` @0x140008604.
- `wWinMain`:
  - `sub_140015C2C` init, `SetErrorMode(0x8001)`.
  - `sub_140011938(args, lpCmdLine)` parses the command line into an argument array.
  - If `/savelangfile` is present it writes the language file and exits.
  - Else `sub_14000FC24` loads an optional language file, and unless `/deleteregkey` is
    present it creates the main window via `sub_140007D74`, loads accelerators 103, and runs
    the message loop with `TranslateAcceleratorW` plus a modeless find dialog.
- `sub_140007D74` registers the class and calls `CreateWindowExW` with class and title both
  `L"CredentialsFileView"`, style 0xCF0000 (WS_OVERLAPPEDWINDOW), and X, Y, width and height
  all 0x80000000 (CW_USEDEFAULT). The recreation keeps the plain `frame.CreateEx(nullptr)`
  default rect to match.
- The readme states the Credentials Decryption Options dialog (template 113) opens at
  startup, which the recreation reproduces.

## Decode path

The DPAPI decrypt wrapper is `sub_140018D58`. It branches on the data source value at
context+2384:

- source 1, 3 or 4 use `CryptUnprotectData` directly on the running system, with optional
  token impersonation for source 3.
- source 2 ("Decrypt Credentials files of any system") uses the offline master-key path
  `sub_140017A4C`, which derives keys from the SYSTEM and SECURITY hives of an external
  drive. The strings `DPAPI_SYSTEM`, `Policy\PolEKList`, `Policy\Secrets`, `lsass.exe`,
  `Microsoft\Protect` and the manual CryptoAPI chain (CryptAcquireContextW, CryptCreateHash,
  CryptHashData, CryptDeriveKey, CryptImportKey, CryptDecrypt, all resolved by
  GetProcAddress) back that path.

### File format

`sub_140019598` reads the whole file, then scans the first 16 bytes for the start of a DPAPI
blob (`sub_1400182B4` validates a blob header). The file is a small CredentialFile header
followed by the blob. The recreation locates the blob the same way by scanning offsets 0
through 15 for the DPAPI provider GUID `df9d8cd0-1501-11d1-8c7a-00c04fc297eb` at blob
offset +4.

`sub_140019488` decrypts the located blob with the wrapper above. `sub_1400193B8` then
dispatches on the first DWORD of the plaintext. A value of 1 is the Windows XP multi-entry
layout, where each entry is length-prefixed and the walk advances by the entry size plus 16.
Any other value is the single-entry Vista and later layout.

### Decrypted CREDENTIAL structure (Vista and later)

`sub_140018E98` reads the fixed header and then walks the length-prefixed value region. The
offsets it reads match the public CREDENTIAL_DECRYPTED layout:

| offset | field | column |
|--------|-------|--------|
| 0x04 | credSize | Decrypted Size |
| 0x0C | type | Entry Type |
| 0x14 | lastWritten (FILETIME) | Modified Time |
| 0x20 | persist | Persist |
| 0x30 | Target (length-prefixed) | Entry Name |
|      | TargetAlias, Comment, UnkData | skipped |
|      | UserName (length-prefixed) | User Name |
|      | credBlobSize plus credBlob | Password |

Type maps 1..6 to Generic, Domain Password, Certificate, .NET Passport, Generic Certificate,
Domain Extended (string ids 651..656). Persist maps 1..3 to Session, Local Machine,
Enterprise (string ids 601..603). Version is XP or Vista/7/8/10 (string ids 551, 552). The
password blob is shown as UTF-16 when a high byte is zero, otherwise as ANSI, which is the
v1.12 fix for an ASCII password string.

## Columns (string ids 1001..1013, default order 0..12)

Filename, Version, Decrypted Size, Modified Time, Entry Type, Persist, Entry Name, User Name,
Password, Full Path, File Size, File Modified Time, File Created Time.

Entry Name, User Name and Password come from the decrypted structure. Modified Time is the
structure's lastWritten. The Filename, Full Path, File Size and the two file time columns come
from the credential file on disk.

## Config -- `HKCU\Software\NirSoft\CredentialsFileView`

Values and defaults recovered from `sub_14001A9A8`:
MarkOddEvenRows (0), ShowGridLines (0), SaveFilterIndex (0), ShowInfoTip (1), VerSplitLoc
(0x3FFF), LowerPaneMode (1 Full Hex Dump, 2 Strings Only), ShowTimeInGMT (0), SaveFileEncoding
(0), DataSource (2), UserName, plus the folder values RegHiveFolder, ProtectFolder1,
ProtectFolder2, CredentialsFolders1, CredentialsFolders2, CredentialsFolders3, and the
column, sort and window-placement blobs Columns, Sort, WinPos. The login password is never
written to the registry in the recreation.

## Commands (menu 102, context 104, accelerators 103)

Save(40001) Exit(40002) About(40003) Refresh(40004) GridLines(40005) HtmlAll(40006)
SelectAll(40007) Copy(40008) Properties(40009) HtmlSelected(40010) ChooseColumns(40011)
AutoSize(40012) Find(40014) DeselectAll(40015) Tooltips(40022) AutoSizeHeaders(40024)
MarkOddEven(40028) DecryptionOptions(40030) ShowTimeGMT(40036) RunAsAdmin(40037)
LowerPaneHex(42501) LowerPaneStrings(42502) SaveEncoding(45700..45703).

## Toolbar (bitmap 110, 112x16 24-bit, 7 icons)

From `sub_140006AD4`, one leading separator then seven buttons: bitmap 6 Decryption Options,
1 Save, 5 Refresh, 2 Copy, 3 Properties, 0 Find, 4 Exit.

## Export / CLI

Seven formats through the older NirSoft engine: Text (/stext), Tab Delimited (/stab), Tabular
(/stabular), HTML Horizontal (/shtml), HTML Vertical (/sverhtml), XML (/sxml), Comma Delimited
(/scomma). Other switches: /nosaveload, /deleteregkey, /savelangfile, /cfg, /StartDecrypt. The
recreation also adds /selftest for the decoder self-check. The HTML report title is the list
caption "Items List" (string id 8), with the "Created by using CredentialsFileView" nirsoft
link. The XML declaration is `<?xml version="1.0" encoding="ISO-8859-1" ?>`.

## Manifest

The application manifest requests no execution level, so it runs asInvoker. Run As
Administrator (Ctrl+F11) relaunches the process elevated through ShellExecute runas.

## Reconstruction

Clean C++20 and WTL under `src/CredentialsFileView/`. `CredFile` holds the file parser and the
CryptUnprotectData decode. `Columns`, `CredRecord`, `Config`, `Export`, `Dialogs` and
`MainFrame` follow the TaskSchedulerView shape. The main window hosts the two panes with a
hand-laid-out horizontal splitter, the same manual layout the original uses.

### Scope boundaries

- The running-system decode path (CryptUnprotectData, data source 1, 3 and 4) is reproduced.
- The offline master-key path for data source 2 (reading external SYSTEM and SECURITY hives
  to derive the DPAPI master keys) is not reproduced. That is a full reimplementation of the
  offline DPAPI key derivation and sits outside this recreation. When CryptUnprotectData
  cannot decrypt a file, the entry is skipped, which matches how the original behaves when a
  file cannot be decrypted.
- The nested second-layer decrypt for WININETCred and Microsoft_WindowsLive data-source
  values is not reproduced.

## Validation

### Decoder self-check (pass)

`CredentialsFileView.exe /selftest` builds a synthetic decrypted Vista structure with known,
non-secret values (Entry Name SelfTestEntry, User Name SelfTestUser, password the synthetic
string SyntheticPass123, type Generic, persist Local Machine, a fixed FILETIME) and confirms
`ParseVistaEntry` decodes every column, plus that `LocateDpapiBlob` finds the provider GUID
past a 12-byte header. Exit code 0 (pass). No real credential value is read, written or shown.

### Top-strip screenshot

`analysis/cfv-toolbar-rebuild.png`, cropped to the title bar, menu and toolbar only. It shows
the light flat toolbar with the seven icons after `PinToLightMode`, matching the original's top
strip (compared side by side against the original run with an empty list). The list area is
empty, so no credential rows appear.

### Launch geometry

Config cleared, then the original and the rebuild launched. Both resolve CW_USEDEFAULT to the
identical default size 1920x1023 with `GetWindowPlacement` showCmd 1. The top-left position
differs only by the operating system's per-launch cascade offset, which is expected for two
separate CW_USEDEFAULT windows. The construction geometry matches.

### Export-format fidelity (structure level)

The original has no clean headless export path and stays at the Decryption Options dialog, so
the export structure was validated against the format-string templates extracted from the
binary and the rebuild's own header-only output against an empty folder. The rebuild emits the
thirteen columns in the correct order for /scomma and /stabular, and the HTML and XML framing
matches the extracted templates: the HTML 3.2 DOCTYPE, the meta content-type, the "Items List"
title, the nirsoft "Created by using" link, the `<th nowrap>` header cells, and the XML
declaration with ISO-8859-1. No real credential data was exported, since the scan folder was
empty.

### Build

MSBuild v143, x64, both configurations clean with 0 errors. Debug 1.27 MB, Release 466 KB with
the static CRT and the repo-wide hardening from `Directory.Build.targets` (Control Flow Guard,
CET shadow stack, EH continuation metadata, high-entropy ASLR, System32-only dependent loading,
full optimization with LTCG).

## Status

Built and validated. Running-system decode path, parser, columns, two-pane UI, config, CLI
switches and the seven export formats reproduced. Offline master-key and multi-user injection
paths are noted above as out of scope.

## Fidelity and HRC pass

Config storage finding and fix. The original does not use the registry for settings. In IDA
the save routine sub_14001A9A8 runs against a config object that sub_140007E14 builds from
the module path with the extension stripped and .cfg appended, writing through
WritePrivateProfileString under the [General] section. The only Software path in the binary
is Shell Folders for data folder resolution, and the RegOpenKey, RegQueryValue and
RegSetValue calls serve the offline hive decode path, not settings. The recreation had stored
settings under HKCU\Software\NirSoft\CredentialsFileView, which was wrong. It now writes
CredentialsFileView.cfg next to the executable, section [General], with the same value names,
and encodes the WinPos and Columns blobs as space separated uppercase hex bytes to match the
original. A launch and close round trip confirmed the rebuild writes a .cfg whose value name
set is identical to the original binary one for one, with no registry key created.

HRC v1.1 light touch. Config.h was rewritten for the .cfg store, using static_cast over C
style casts, nullptr, initialized variables and the nodiscard attribute on the value
returning helpers. The ColKind column enum became an enum class with its enumerator names
kept. The /deleteregkey handler now removes the .cfg file. One console write size cast became
static_cast.

Rebuild and re-validation. Debug and Release x64 rebuilt clean with v143. The decoder self
check (/selftest) passes. The ToolbarWindow32 empty area reads 240,240,240 after the startup
dialog is dismissed. The window renders at the default size with showCmd normal.

## MainFrame header and source split

Code-motion refactor following the WinObj golden template (commit 4a3adc0). The CMainFrame
method bodies moved out of MainFrame.h into a new MainFrame.cpp, so the header now carries
only the class shape. Behaviour is unchanged, and only whitespace and layout differ, from the
HRC clang-format pass over both files.

- MainFrame.h, 652 lines before and 193 lines after. It keeps the members, the
  DECLARE_FRAME_WND_CLASS macro, the message map verbatim (including the raw m_uFindMsg
  dispatch line), the static constexpr kBar, and every method declaration. PreTranslateMessage
  keeps its override on the declaration, and BuildHexDump and BuildStrings keep static on the
  declaration.
- MainFrame.cpp, new, 875 lines and 23605 bytes. It includes MainFrame.h plus the tool's own
  CredRecord, CredFile, Columns, Export, Config and Dialogs headers and the shared UiUtil, then
  the out-of-line CMainFrame definitions in source order. The override and static keywords are
  dropped on the definitions, the WTL handler signatures keep their trailing BOOL&, and the
  OnEraseBkgnd to FillFrameBtnFace and PinToolbarLight calls are byte for byte the same.
- CredentialsFileView.vcxproj gained MainFrame.cpp under ClCompile. The thin entry unit
  CredentialsFileView.cpp is left as is.

### Rebuild (pass)

MSBuild v143 with /t:Rebuild, both x64 configurations clean with 0 errors. The only warning is
the pre-existing repo-wide D9025 from the inline-depth hardening flag, which is present before
this change.

### Shell and layout re-validation (pass, no credential loaded)

The rebuilt Release binary was launched with no /StartDecrypt switch, and the startup
Credentials Decryption Options dialog was cancelled, so no decrypt ran and no credential file
was read. The empty shell was then probed over Win32:

- Frame window class CredentialsFileView renders at 1920x1023 with GetWindowPlacement showCmd
  1, the same construction geometry recorded above for the original and the prior build.
- Frame children all present and visible: ToolbarWindow32 (1904x26), msctls_statusbar32
  (1904x23), SysListView32 (1900x590) with its SysHeader32 column header (1900x24), and the
  lower Edit pane (1883x296). The hand-laid two-pane splitter layout holds.
- The toolbar child, found through FindWindowExW(frame, 0, ToolbarWindow32, null), reads
  240,240,240 in its empty band, sampled at x=1809 of the 1904 wide band, confirming the
  PinToolbarLight fix still pins the toolbar to light mode after the split.

No credential value was read, shown, logged or captured. The decoder crypto round-trip was
validated against the synthetic self-check vector originally, and this change is code motion
only, so it was not re-run against real data.
