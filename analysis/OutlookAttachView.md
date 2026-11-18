# OutlookAttachView -- reverse-engineering notes

Target: `targets/outlookattachview-x64/OutlookAttachView.exe` -- NirSoft
OutlookAttachView v3.55, a viewer for the attachments across the messages in the
user's Outlook mailbox. **x64 PE, Unicode, not packed.** md5
`ce25302f310c859955212ae299700943`, sha256
`e03cf8b7f246a08c6af5e53d162b7da1462f83024ac8a83d6eb293e1b0a391e5`. Image base
0x140000000. 749 functions. Engines used -- IDA idalib (session on
`OutlookAttachView.exe.i64`), plus the extracted resources from Resource Hacker.
The static CRT and VC runtime code dominate the function count, the UI strings
are UTF-16 in `.rdata` and `.rsrc`.

## Data source -- Extended MAPI through mapi32.dll (loaded dynamically)
The original does not statically link the MAPI logon entry points. `sub_14001D2F0`
loads `mapi32.dll` with `LoadLibraryEx` and resolves `MAPIInitialize`,
`MAPIUninitialize`, `MAPILogonEx` and `FreePadrlist` with `GetProcAddress`. The
MAPI utility functions `MAPIGetDefaultMalloc`, `MAPIAllocateBuffer`,
`MAPIAllocateMore`, `MAPIFreeBuffer`, `OpenIMsgSession`, `CloseIMsgSession` and
`OpenIMsgOnIStg` are the only MAPI imports in the static import table.

Logon uses `MAPILogonEx` with the default profile and no logon dialog. The scan
then walks the message stores, folders, messages and attachments through the
Extended MAPI interfaces. The per-message reader is `sub_14001DFC8`, which opens
the recipient table, reads the message property set, and opens the attachment
table.

The recreation reproduces this exactly. `MapiScanner.cpp` loads `mapi32.dll` the
same way, logs on with `MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL |
MAPI_USE_DEFAULT | MAPI_UNICODE` and no `MAPI_LOGON_UI`, then runs
`GetMsgStoresTable` to `OpenMsgStore` to the IPM subtree to a recursive folder
walk. Each folder reads its contents table for messages and its hierarchy table
for subfolders. Each message reads its property set, its recipient table and its
attachment table, and emits one row per attachment.

### MAPI headers
The modern Windows SDK ships only Simple MAPI (`mapi.h`), not the Extended MAPI
headers the original uses. Installing the Outlook MAPI headers was out of scope,
so `MapiMin.h` declares the small subset of structures, property tags and
interface vtables the scanner needs. Only the methods the scanner calls carry
real signatures, the other vtable slots are pure-virtual placeholders that keep
each interface's layout at the offsets the real MAPI objects use. The module
still loads `mapi32.dll` at run time, so it also launches on a machine with no
Outlook and no profile.

## Fields read per attachment (22 columns, string ids 1001..1022)
Message-level fields are shared by every attachment of that message. The
attachment-level fields come from the attachment table row.

| Source | Property | Columns |
|--------|----------|---------|
| Message | PR_SUBJECT, PR_SENDER_NAME, PR_SENDER_EMAIL_ADDRESS, PR_DISPLAY_TO, PR_CREATION_TIME, PR_MESSAGE_DELIVERY_TIME, PR_MESSAGE_FLAGS, PR_ENTRYID | From, To, Subject, Created On, Message Delivery Time, Message Direction, From Email, To Email, Domain, Message EntryID |
| Recipient table | PR_DISPLAY_NAME, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS, PR_RECIPIENT_TYPE | To, To Email |
| Transport headers | PR_TRANSPORT_MESSAGE_HEADERS (parsed) | Computer Address, Email Software |
| Attachment table | PR_ATTACH_LONG_FILENAME, PR_ATTACH_FILENAME, PR_ATTACH_NUM, PR_ATTACH_METHOD, PR_ATTACH_SIZE, PR_ATTACH_MIME_TAG, PR_ATTACH_CONTENT_ID, PR_RENDERING_POSITION | Filename, MIME Type, Attachment Type, File Size, Attachment ID, Inline, Extension |

Formatting rules recovered from the binary.
- **Attachment Type** maps PR_ATTACH_METHOD through string ids 101 By Value, 104 By Reference (methods 2, 3 and 4), 105 Embedded Item, 106 OLE.
- **Computer Address** is taken from the `Received: from` header line, **Email Software** from the `x-mailer:` header line, both parsed out of the transport headers. The original scans the headers in `sub_14001DFC8`.
- **Inline** is Yes when the attachment has a content id or a rendering position, No otherwise. The `Show Inline Attachments` option controls whether inline rows appear at all, and the original turns it on by default.
- **Message Direction** is Outgoing when PR_MESSAGE_FLAGS carries MSGFLAG_FROMME, Incoming otherwise, with string ids 1201 and 1202.
- **File Size** honours the File Size Unit option with Bytes (grouped by the locale thousands separator), KB, MB, GB and Automatic.
- **Created On** and **Message Delivery Time** come from the message timestamps, shown in local time unless Show Time In GMT is on.
- **Domain** is the part after the last `@` of the from or to email.

## Config -- an INI .cfg file next to the executable
OutlookAttachView stores its settings in an INI file read with
`GetPrivateProfileInt` and written with `WritePrivateProfileString`, not in the
registry. The default file is `OutlookAttachView.cfg` next to the executable, or
the file named by `/cfg`. The value names were recovered from `sub_140002524`,
among them `ShowGridLines`, `ShowInfoTip`, `ShowInlineAttachment`,
`ShowTimeInGMT`, `BlackBackground`, `MarkOddEvenRows`, `MarkInlineAttachments`,
`AlignNumbersToRight`, `AddExportHeaderLine`, `AlwaysOnTop`, `TrayIcon`,
`PreviewPane`, `ShowOptionsBeforeScan`, `FileSizeUnit`, `SaveFileEncoding`,
`SaveFilterIndex`, `DoubleClickAction`, `EnterKeyAction`, `CopyFilesFolder`,
`FilenameFormat`, and the full `ScanOptions.*` group that drives the Mailbox Scan
Options window. The recreation reads and writes the same value names and stores
the window placement as a hex blob under `WinPos`.

## Command-line switches
Export, each taking a filename: `/stext /stab /stabular /scomma /shtml /sverhtml
/sxml` (dispatch confirmed in `sub_14001C7A8`, there is no `/sjson` in this
version). Also `/cfg <file>`, `/start`, `/nosaveload`, `/SaveDirect`, `/sort
<column|~column>` (repeatable), `/nosort`, `/Columns <comma list>`, and the many
`/ScanOptions.*` and extract switches. Present in the binary and not
reconstructed -- `/extractall`, `/extractallsetreadflag`, `/extractdeleteall`,
`/LogFile`, `/LogFileAppend`, `/savelangfile`. These extract real attachments and
are out of scope for the offline recreation.

## Export formats and encodings (recovered by decompiling the writers)
OutlookAttachView uses the older NirSoft HTML template, not the CSS `mytable`
template that the newer tools use.
- `/scomma` -- UTF-8 with a BOM. Optional header line. Comma-separated, no trailing comma, a field is quoted when it holds a comma, quote or line break, and an inner quote is doubled. Logic from `sub_14000F9B0`.
- `/stext` -- UTF-16LE with a BOM. Each record wrapped in a 50-character rule, field name left-justified to a minimum width then a colon and a space.
- `/stab` and `/stabular` -- UTF-16LE with a BOM, tab-separated and fixed-width.
- `/shtml` and `/sverhtml` -- UTF-16LE with a BOM. `<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">`, then `<html><head><title>Outlook Attachments</title></head>` and `<body> <h3>Outlook Attachments</h3>`, a `Created by using` credit line to nirsoft.net, and a `<table border="1" cellpadding="5">` with `<th width="N">` header cells and `<td bgcolor=#FFFFFF nowrap>` data cells. Templates from `sub_1400042C0`, `sub_140010630`, `sub_14000FEFC` and `sub_1400100C8`.
- `/sxml` -- UTF-16LE with a BOM. `<?xml version="1.0" ?>`, a root tag slugged from the report title (`outlook_attachments`), and each `<item>` with column tags slugged by `sub_14000F614` (lower-case, spaces and slashes and parentheses replaced with an underscore).

The `SaveFileEncoding` option forces ANSI, UTF-16 or UTF-8 for every format. When
it is Default, CSV is UTF-8 and every other format is UTF-16, which the
reconstructed `Export.cpp` reproduces.

## UI (resources)
Main menu 102, context menu 104, stop menu 109, tray menu 112, accelerators 103.
Dialogs -- Properties 105, Exception 107, Copy Selected Files To 110, Select
Outlook Folders 111, About 112, Quick Filter 114, Column Settings 1096, Mailbox
Scan Options 1100. Toolbar bitmap 104 is a 160x16 4-bit strip of ten 16x16
images, built with `CreateToolbarEx` in `sub_140014AE4`. The twelve toolbar
entries are a leading separator, Mailbox Scan Options (image 8), Copy To (image
6), Delete (image 7), a separator, Export (image 1), Refresh (image 5), Copy
(image 2), Properties (image 3), Find (image 0), Sort By (image 9, a dropdown)
and Exit (image 4). Bitmaps 133 and 134 are 14x14 header sort arrows for the
list column header. The main window is created with `CreateWindowExW` in
`sub_14001BC34` with class and caption `OutlookAttachView`, style
`WS_OVERLAPPEDWINDOW`, and CW_USEDEFAULT for X, Y, width and height.

## Reconstruction
Clean C++20 plus WTL under `src/OutlookAttachView/`, x64 Unicode, output
`OutlookAttachView.exe`. Files -- `OutlookAttachView.cpp` (WinMain and the
command-line export path), `MapiMin.h` (the self-contained Extended MAPI subset),
`MapiScanner.cpp/.h` (the dynamic mapi32.dll load, the logon, the store, folder,
message and attachment walk, and the save and delete code path),
`AttachRecord.h` (the 22 precomputed cells plus sort keys), `Columns.h`,
`Export.cpp/.h` (seven formats), `Config.h` (the INI config), `MainFrame.h`,
`Dialogs.h`, `resource.h`, `res/OutlookAttachView.rc`. The toolbar is built with
`LoadToolbarImageList(IDB_TOOLBAR)` from the shared `common/UiUtil.h`, which keys
the strip's light-gray corner transparent so the icons render in colour rather
than as a dark box. The main window is created with WTL's default rect to match
the original's CW_USEDEFAULT, and saved placement overrides it on later launches.
The light toolbar fix is in place -- `PinToLightMode` in WinMain, `PinToolbarLight`
after the toolbar is made, and the `WM_ERASEBKGND` handler that fills the frame
with the button-face colour. asInvoker manifest, so the tool launches with plain
Start-Process. Links comctl32, shell32, advapi32, comdlg32, ole32, oleaut32,
gdi32 and version. MAPI is loaded dynamically, so no mapi32.lib is linked.

## Security note
This tool reads a mailbox, so the recreation was validated offline only. No real
mailbox, message or attachment was connected to, read, enumerated or
transmitted. This machine has no MAPI profile (the Windows Messaging Subsystem
Profiles registry key is absent), so `MAPILogonEx` fails at logon and the scan
never reaches any mail. The enumeration and the save and delete paths were
verified as code paths that compile and run, not against real data. The screen
captures were cropped to the top strip with an empty list.

## Deviations
- The Mailbox Scan Options window (dialog 1100) and its full filter set are carried in the config but not shown. The recreation scans straight away rather than prompting. The scan still honours Show Inline Attachments, Show Time In GMT and File Size Unit.
- The attachment extract via PR_ATTACH_DATA_BIN works for by-value attachments. A very large attachment that MAPI exposes only as a stream is a known ceiling of the GetProps-based read.
- The extract command-line switches, the language-file dump, the preview pane, Quick Filter, the tray icon and custom fonts are carried in the settings and the menus but only partially wired.
- The config file layout is a clean INI with the original value names rather than the original's exact binary blob structure for some values. The value names and semantics match.
- The recreation uses a stable sort. The original uses `qsort`, which is not stable, so rows that share a sort key can appear in a different relative order.

## Status -- DONE, validated offline
- Debug and Release both build for **x64** with 0 errors. Release carries the full mitigation and optimization set from `Directory.Build.targets` (Control Flow Guard, CET, EH continuation metadata, high-entropy ASLR, System32-only dependent loading, full optimization, static CRT). The only Release warnings are the benign `/Ob2` to `/Ob3` override that every tool in the tree gets from the shared targets.
- Launch opens the window and reports that no Outlook profile was found, then shows an empty list, with no crash. Screenshot at `analysis/OutlookAttachView-rebuild.png` (title, the File Edit View Options Help menu, the light toolbar with colour icons, and the correct columns).
- Launch geometry -- both the original and the rebuild open at **1920 by 1023** with showCmd 1 (config cleared on both). The size matches, the top-left differs only by the non-deterministic CW_USEDEFAULT cascade offset.
- Light toolbar -- the `ToolbarWindow32` child's empty area reads **240,240,240** on the rebuild, the same as the original, so the de-themed bar is light.
- `/s*` export format -- validated structurally against the binary's own format-string templates with synthetic records (no mail). CSV quoting, the HTML 3.2 bgcolor table, the vertical HTML, and the XML slug tags all match. BOMs confirmed -- CSV is `EF BB BF` (UTF-8), XML and HTML are `FF FE` (UTF-16LE).
- Mailbox enumeration was validated as a code path only, with no real mail accessed.

## Runtime validation

Date 2026-11-17. Outlook is installed and `mapi32.dll` is present, but the machine has no
MAPI profile (the Windows Messaging Subsystem Profiles key holds no profile and no default).
asInvoker, so the tool ran with a plain launch. No real mailbox, message or attachment was
connected to, read or shown.

### Real mapi32.dll logon path executes
A probe compiled over the tool's own `MapiScanner.cpp` ran `MapiAvailable()` and
`ScanMailbox()` against the installed MAPI:

```
MapiAvailable()        : 1
ScanMailbox status     : NoProfile
HRESULT                : 0x80040401   (MAPI_E_LOGON_FAILED)
records enumerated     : 0
message                : No Outlook profile was found. ...
```

`MapiAvailable()` 1 means `mapi32.dll` loaded and `MAPIInitialize`, `MAPIUninitialize`,
`MAPILogonEx` and `MAPIFreeBuffer` all resolved. `MAPIInitialize` then succeeded and
`MAPILogonEx` ran against the real MAPI and returned `MAPI_E_LOGON_FAILED` because there is
no profile, which the scanner maps to NoProfile and handles without a crash. This is the step
up from the offline run, where `mapi32.dll` was absent and the loader returned NoMapi.

### GUI and headless both report no profile cleanly
The Release GUI launched and showed the modal report "No Outlook profile was found. Open
Outlook with a mail profile and run OutlookAttachView again.", then an empty list, no crash.
The headless export path ran on both the rebuild and the original as `OutlookAttachView.exe
/cfg <temp> /scomma <temp.csv>`. Both exited 0 and wrote no rows (no profile, header line
off), matching each other and matching a headless run with no Outlook session.

### Full enumeration not validated, and why
A temporary PST-only profile holding one synthetic message and one synthetic attachment was
considered and not attempted, for two reasons that together make it neither safe nor cleanly
reversible on this machine:

1. The scanner logs on with `MAPI_USE_DEFAULT`, no profile name and no logon UI, so it can
   only reach the machine default profile, and the recreation wires no `/profile` switch.
   Validating enumeration would mean creating a profile and making it the system default,
   mutating the HKCU Windows Messaging Subsystem state on a box that currently has none.
2. No Extended MAPI SDK is present (only a MinGW `mapix.h` under CLion, and no `edkmdx.h`, no
   MFCMAPI, no Redemption), so scripting profile creation and message injection would mean
   hand-declaring the profile-admin and PST provider interfaces, with real risk of leaving
   messaging-subsystem state behind.

Per the task's own fallback, full enumeration validation needs a configured Outlook account
that is not the user's real one. The enumeration, save and delete paths stay validated as
code only. The profile state was left as found (no profile, no default).

## Fidelity and HRC pass

Date 2026-11-17. A config-storage fidelity review against the original plus a light
HRC v1.1 touch on the one file changed. This closes the config deviation listed earlier.

### Config storage, three fixes
The original keeps OutlookAttachView.cfg next to the exe (or the file named by /cfg) and
reads and writes it through the shared NirSoft config layer. Three things did not match
the original and were corrected in Config.h.

- Section name. The config object constructor sub_140022558 copies L"General" into the
  section field regardless of the tool name, so every value lives under [General]. The
  recreation used [OutlookAttachView]. Changed to [General].
- Columns. The writer sub_14000E368 stores one 4-byte record per displayed column through
  the binary-blob method, and sub_14000DEA0 fills each record with the column width (LVCF
  width) then its order index. The recreation wrote a comma list of width:index pairs.
  Changed to the 4-byte-record blob, width (LE WORD) then internal index (LE WORD).
- WinPos. sub_140001F6C stores the 44-byte WINDOWPLACEMENT through the same blob method.
  The serializer sub_14000B064 formats each byte with "%2.2X ", so the blob is uppercase
  hex pairs joined by a single space with no trailing space, the same layer the old-gen
  sniffers use. The recreation wrote unspaced hex. Changed to the space-separated form.

The scalar and string value names (ShowGridLines, FileSizeUnit, SaveFileEncoding and the
rest) were already correct, confirmed in the reader sub_140002524, and Sort stays a signed
decimal through the integer writer, also already correct.

### HRC edits
In the same Config.h the C-style casts in the profile getter and the old hand-rolled hex
reader or writer became static_cast, the manual reinterpret_cast byte loop for WinPos was
replaced by a memcpy over the parsed blob, and the new toHex and fromHex helpers carry
[[nodiscard]]. The unscoped enum ColKind was left as is. It is used across several files,
so an enum class switch would ripple and risk a behaviour change, which the surgical rule
rules out.

### Rebuild and re-validation
- Debug and Release x64 rebuilt clean with VS 2022 v143, only the usual /Ob2 to /Ob3
  override warning.
- Config round-trip over the tool's own Config.h. A seeded config saved to a .cfg and
  reloaded round-trips every field. The raw file shows [General] and not
  [OutlookAttachView], the Columns value is the space-separated 4-byte-record blob, WinPos
  is the space-separated placement blob, and Sort=-5. PASS.
- Export unchanged. The headless /scomma on the Release binary with no profile exits 0 and
  writes the UTF-8 byte-order-mark output with the header line off, as before.
- First-launch geometry 1920 by 1023, matching the original, with the cascade position
  differing only by the CW_USEDEFAULT offset.
- Light toolbar. The rebuild's ToolbarWindow32 empty area reads 240,240,240 at four points
  and the cropped strip shows the colour icons on a light band.

This supersedes the earlier Deviations note that the config used a clean INI rather than
the original's exact blob structure. The section, the Columns blob and the WinPos blob now
match the original byte format.
