# SniffPass -- reverse-engineering notes

Target: `targets/sniffpass-x64/SniffPass.exe` -- NirSoft SniffPass v1.13, x64 PE, ANSI
(MultiByte, VC2005 era). sha256 `c92580318be4effdb37aa67145748826f6a9e285bc2426410dc280e61e3c7620`,
md5 `b68e5f362582d15b2c904133f80bdfa6`. Image base 0x140000000. 317 functions, 351
strings. requireAdministrator manifest with comctl32 v6. Engine used: IDA idalib MCP
session `sniffpass`.

## Entry / startup
- `start` @0x140010CB0 is the VC CRT stub, then `WinMain` @0x14000C70C.
- `WinMain`:
  - LoadLibrary comctl32.dll and call InitCommonControlsEx, on failure MessageBox
    "Error: Cannot load the common control classes." and return 1.
  - LoadLibrary shell32.dll and resolve SHGetSpecialFolderPathA dynamically.
  - CoInitialize, WSAStartup(2).
  - Parse the command line into an arg list.
  - If `/nocapdriver` is absent, call the WinPcap loader `sub_140010A10`.
  - If `/savelangfile` is present, write the language file and return.
  - If `/deleteregkey` is present, RegDeleteKeyA `HKCU\Software\NirSoft\SniffPass` and return.
  - CreateWindowExA with class "SniffPass", title "SniffPass", style 0xCF0000
    (WS_OVERLAPPEDWINDOW), X=0 Y=0 width=640 height=480.
  - Message loop with TranslateAcceleratorA and IsDialogMessageA.

## Capture backend (the key finding)
SniffPass has four capture methods, chosen in the Capture Options dialog (resource 108)
and stored as `CaptureMethod` in the cfg:

| CaptureMethod | Method | How it is reached |
|---------------|--------|-------------------|
| 0 | Raw Sockets (Windows 2000/XP) | WS2_32 statically linked, window class `PacketSnifferClass1`, `WSAIoctl` with SIO_RCVALL on a raw socket, no driver needed |
| 1 | WinPcap Packet Capture Driver | `wpcap.dll` loaded dynamically |
| 2 | Network Monitor Driver | `npptools.dll` loaded dynamically |
| 3 | Network Monitor Driver 3.x | `NmApi.dll` loaded dynamically |

There is no static import of `wpcap.dll`, `NmApi.dll` or `npptools.dll`. Each driver
backend is loaded with LoadLibraryA and resolved with GetProcAddress. `sub_140010A10`
loads `wpcap.dll` and resolves the full set of pcap entry points (pcap_findalldevs_ex,
pcap_freealldevs, pcap_open, pcap_close, pcap_next_ex, pcap_loop, pcap_compile,
pcap_setfilter, pcap_freecode, pcap_next, pcap_findalldevs, pcap_open_live,
pcap_open_offline, pcap_dispatch). If the library fails to load or any entry point is
missing, it frees the module and reports the backend unavailable. The BPF filter string
is `ip proto \tcp or \udp or \icmp`. Adapter enumeration uses `GetAdaptersInfo` from
`iphlpapi.dll`, loaded dynamically, plus the registry key
`Software\Microsoft\Windows NT\CurrentVersion\NetworkCards`.

The recreation reproduces this dynamic loading in `Capture.cpp`. The pcap, NmApi and
npptools function names are declared inline, so the project builds and launches with no
capture SDK and no driver installed. When a driver method is selected and its DLL is
absent, the probe reports the backend missing and the tool shows the original's failure
text (STRINGTABLE 9) rather than crashing.

### No live capture in the recreation (security)
This tool handles credentials, so the recreation performs no live capture at all. No
socket is put into receive mode and no packet is ever read. The only credential parsing
that runs is the self test against synthetic vectors. Start Capture probes the selected
backend and either reports the driver missing or states that live capture is disabled by
design. Import From Capture File is also declined, because a user supplied capture file
could hold real traffic. No real credential is captured, logged, shown or transmitted.

## Credential parser (`sub_14000DCF0`)
The parser runs over one reassembled TCP connection, modelled as an ordered list of
segments (each segment is one captured packet payload). Each segment is split into CRLF
lines, and the protocol state machines key off the segment index, the same way the
original keys off the packet index. Helper functions confirmed by decompilation:

- `sub_140004DFC` case insensitive substring search, returns index or -1.
- `sub_14000DBB8` space tokenizer, drops empty tokens.
- `sub_140001CA8` base64 decode, standard table (A-Z a-z 0-9 + /), stops at the first pad.
- `sub_140004F98` bounded string copy.
- `sub_14000E810` emit a record with the local and remote endpoints swapped.

Detection rules, per protocol:

- POP3: a line beginning `USER ` captures the user. The server reply in the next segment
  beginning `+` marks POP3. A line beginning `PASS ` captures the password and emits.
- FTP: same USER and PASS flow, but the server reply in the next segment beginning with a
  digit (for example `331`) marks FTP.
- IMAP: a line with `login ` not at the start, tokenized to four tokens with the second
  equal to `login`, is `<tag> LOGIN <user> <pass>`. The record emits when a later line
  begins with the stored tag.
- SMTP: a line `AUTH LOGIN` opens the state and records its segment index. The two `334 `
  server prompts are counted, the base64 username arrives two segments after AUTH, the
  base64 password four segments after, and the record emits once both prompts are seen.
- HTTP basic: a first line beginning `get ` marks the connection, then a later line
  beginning `authorization` whose second to last token is `basic` has its last token
  base64 decoded and split on `:` into user and password.

Protocol codes match STRINGTABLE 1100..1105: 0 Unknown, 1 SMTP, 2 POP3, 3 IMAP, 4 FTP,
5 HTTP.

## Columns (STRINGTABLE 1001..1009)
Nine columns, array index is the field index. Default widths from the cfg Columns blob.

| # | Title | default width |
|---|-------|---------------|
| 0 | Index | 60 |
| 1 | Protocol | 60 |
| 2 | Local Address | 100 |
| 3 | Remote Address | 100 |
| 4 | Local Port | 80 |
| 5 | Remote Port | 80 |
| 6 | User | 80 |
| 7 | Password | 80 |
| 8 | Capture Time | 130 |

## UI
- Main window: a single owner-data SysListView32 (LVS_OWNERDATA, id 0x103), toolbar
  (CreateToolbarEx, strip bitmap 104, id 0x102) and status bar (id 0x101). Menu 102.
- Toolbar strip 104 holds seven images. Image order, recovered from the TBBUTTON array in
  `sub_1400098D0`: 0 Find, 1 Save, 2 Copy, 3 Properties, 4 Exit, 5 Start Capture, 6 Stop
  Capture. Button order: Start, Stop, separator, Save, Copy, Properties, Find, separator,
  Exit.
- Dialogs: Capture Options (108), Properties (105), Column Settings (1096), About (112),
  Exception (107).
- Status text: "%d item(s)" plus ", %d Selected" when there is a selection.

## Config -- SniffPass.cfg, section [General]
File based since version 1.03, read with GetPrivateProfileIntA and
GetPrivateProfileStringA. Keys: ShowGridLines, SaveFilterIndex, ExcludeDuplicates,
BeepOnNewPassword, AddExportHeaderLine, AddToWindowsFirewall, CaptureMethod,
PCapAdapterName, AdapterName, NetMonAdapterName, NetMon3AdapterName, plus the WinPos and
Columns hex blobs and the Sort integer. A legacy registry key
`HKCU\Software\NirSoft\SniffPass` still exists only for `/noloadsettings` and
`/deleteregkey` cleanup.

## Export
Seven formats, filter and order from STRINGTABLE 501..507: Text, Tab delimited, Tabular,
HTML Horizontal, HTML Vertical, XML, Comma delimited. Report title "Sniffed Passwords"
(STRINGTABLE 8). The HTML follows the HTML 3.2 template with the "Created by using"
nirsoft link, and the XML declares ISO-8859-1. Output is single byte ANSI with no byte
order mark, matching the ANSI build. The Tab and Comma formats carry a header line only
when Add Header Line is on.

## Command line switches
Present in this binary: `/noloadsettings`, `/noreg`, `/nocapdriver`, `/savelangfile`,
`/deleteregkey`. This x64 v1.13 binary carries no `/stext` style save-and-quit switches.
The export engine is reached through File > Save in the original. The recreation keeps the
original switches and adds the standard NirSoft `/s*` headless export plus a `/selftest`
mode, both for offline validation. The `/s*` and `/selftest` paths are the recreation's
own, noted here so the difference from the binary is on the record.

## Validation

### Per-protocol parser self check (synthetic vectors, `/selftest`)
Each vector is a hand built byte stream with invented values. Run on the Release binary:

```
POP3: PASS
FTP: PASS
IMAP: PASS
SMTP: PASS
HTTP: PASS
parser: ALL PASS
```

Vectors used:
- POP3 `USER alice` / `+OK` / `PASS wonderland` -> POP3, alice, wonderland
- FTP `USER bob` / `331 Password required` / `PASS buildr` -> FTP, bob, buildr
- IMAP `a1 LOGIN carol p@ssw0rd` / `a1 OK LOGIN completed` -> IMAP, carol, p@ssw0rd
- SMTP `AUTH LOGIN` / `334 VXNlcm5hbWU6` / `ZGVtbw==` / `334 UGFzc3dvcmQ6` / `czNjcjN0`
  -> SMTP, demo, s3cr3t
- HTTP `GET ... Authorization: Basic d2VidXNlcjp3M2JwNHNz` -> HTTP, webuser, w3bp4ss

### Build
Clean build with VS 2022 v143, Debug x64 and Release x64, both 0 errors. The only Release
warnings are D9025 "/Ob2 overridden with /Ob3", emitted for every tool by the shared
`Directory.Build.targets`. The Release binary is x64, 454656 bytes, static CRT, with the
requireAdministrator manifest embedded from the extracted `res\SniffPass.rc`.

### Export format match
`/selftest <dir>` writes the seven formats from synthetic rows. Checked against the
binary's own template strings: HTML 3.2 DOCTYPE, `<table border="1" cellpadding="5">`,
header row `bgcolor="E0E0E0"`, data cells `<td bgcolor=#FFFFFF nowrap>`, the "Created by
using" nirsoft anchor, the vertical layout `<tr><td nowrap><b>%s</b>`, the XML header
`<?xml version="1.0"  encoding="ISO-8859-1" ?>` with lowercased tag names joined by `_`, the text
layout `%-18s: %s` with `=` separators, and tab and comma header lines. All match. Files
are single byte ANSI with no byte order mark.

### Launch geometry match
Rebuild first launch window rect is L=0 T=0 R=640 B=480, size 640x480, matching the
original CreateWindowExA(X=0, Y=0, width=640, height=480, WS_OVERLAPPEDWINDOW).

### Light toolbar
The rebuild carries the comctl32 v6 manifest, so the three edit toolbar fix is applied:
PinToLightMode before InitCommonControls, PinToolbarLight after the toolbar is made, and
the WM_ERASEBKGND handler returning FillFrameBtnFace. The ToolbarWindow32 child of the
elevated rebuild was captured and its empty area reads 240,240,240 at x = 560, 600, 610
and 620. Cropped top strip screenshot (toolbar and menu only, empty list):
`analysis/SniffPass-toolbar.png`.

## Anything that did not match
- The original x64 v1.13 binary has no `/s*` save-and-quit switches. The recreation adds
  them for offline validation. Noted above.
- The recreation performs no live capture, so no rows are produced at run time. This is a
  deliberate security choice, not a fidelity gap in the parser, which is validated against
  synthetic vectors.
- Import From Capture File is declined in the recreation for the same security reason.
- The original uses fixed 129 byte user and password buffers in the parser. The recreation
  uses std::string, keeping full data fidelity, a deliberate improvement over the original
  truncation.

## Runtime validation

Date 2026-11-17. Npcap is installed, but this build performs no live capture by design (see
"No live capture in the recreation"). SECURITY-CRITICAL tool, so the credential path was
driven with a real loopback exchange fed into the tool's own parser, using only invented
values. No real login, mail server or account was involved.

### Live loopback exchange, invented credentials
A throwaway in-process server bound `127.0.0.1` on an ephemeral port. A client connected over
loopback and ran an unencrypted POP3 USER/PASS exchange. The three application payloads that
crossed the loopback socket, in wire order:

```
seg0 (client to server): USER testuser\r\n
seg1 (server to client): +OK testuser welcome, send PASS\r\n
seg2 (client to server): PASS fakePassword123\r\n
```

Every value here was invented for this run.

### The real parser recovered the pair
Those three captured payloads were fed into the tool's own `ParseConnection`
(`CredParser.cpp`, compiled unchanged into a probe). It returned exactly one credential:

```
credentials recovered  : 1
  protocol=POP3 user=[testuser] pass=[fakePassword123]
```

So the behavioural core of SniffPass reassembles the loopback POP3 flow and recovers the
username and password that were sent, keyed off the segment order the same way a live capture
would feed it.

### Shipped-binary self-check and GUI launch
`SniffPass.exe /selftest` on the Release binary, run elevated, passed every protocol: POP3,
FTP, IMAP, SMTP and HTTP all PASS, with "parser: ALL PASS", and it wrote the seven export
formats. The Release GUI launched elevated, created its window (class `SniffPass`, 640 by
480), stayed responsive and did not crash. A cropped top strip (toolbar only, empty list, no
credential shown) confirmed the light toolbar band with colour icons.

### Not validated
The recreation does not open a socket in receive mode and declines capture files, so the
parser was reached through the loopback bridge above rather than through Npcap. That is the
deliberate security boundary, not a parser gap. No real authentication traffic was touched.

## Fidelity and HRC pass

Date 2026-11-17. A config-storage fidelity review against the original plus a light
HRC v1.1 touch on the one file changed. No behaviour change beyond the config byte
format.

### Config storage, one fix
The shipped SniffPass.cfg in targets is ground truth. It confirms section [General] and
every value name already matched. The gap was the binary value encoding. The shared
NirSoft config layer writes WinPos and Columns as uppercase hex byte pairs joined by a
single space with no trailing space, for example "2C 00 00 00". The recreation wrote the
same bytes as one unspaced run, so it could neither read the shipped cfg's WinPos and
Columns nor produce a cfg the original could read.

Fixed in Config.h. toHex now joins the pairs with a single space and no trailing space,
and fromHex skips the separators, so it still reads any older unspaced value too. The
Columns record stays width (LE WORD) then internal index (LE WORD), and Sort stays a
signed decimal, both already correct.

### HRC edits
In the same Config.h the C-style casts in the profile getters and the column packing
became static_cast, and toHex and fromHex gained [[nodiscard]]. The unscoped enums
CaptureMethod, ColKind and Protocol were left as is. Each is used across several files and
some values are stored or compared as integers, so an enum class switch would ripple and
risk a behaviour change, which the surgical rule rules out.

### Rebuild and re-validation
- Debug and Release x64 rebuilt clean with VS 2022 v143, only the usual /Ob2 to /Ob3
  override warning.
- Config interop over the tool's own Config.h. Loading the shipped SniffPass.cfg recovers
  the placement, CaptureMethod 1, the adapter GUID and all nine columns with their widths,
  and re-saving emits WinPos and Columns byte-for-byte identical to the shipped file. PASS.
- Parser self-check on the Release binary, run elevated. POP3, FTP, IMAP, SMTP and HTTP
  all PASS, with parser ALL PASS, and the seven export formats were written.
- Export formats stay intact. The XML declares ISO-8859-1, the HTML carries the HTML 3.2
  DOCTYPE, and the comma format is single-byte ANSI with no byte order mark.
- First-launch geometry 640 by 480, matching the original.
- Light toolbar. The elevated rebuild's ToolbarWindow32 empty area reads 240,240,240 at
  four points and the cropped strip shows the colour icons on a light band.
