# SmartSniff (smsniff) reconstruction

Source-accurate C++20 and WTL recreation of NirSoft SmartSniff v2.30, x64,
requireAdministrator. Original binary `targets/smsniff-x64/smsniff.exe` (MD5
64ad58887ec43d0de241db2fafda24fb). Project `src/smsniff/`.

## What it is

A packet sniffer. The upper pane shows one row per TCP, UDP or ICMP conversation.
The lower pane shows the selected conversation as a hex dump or an ascii stream. The
original offers three capture backends and a `/s*` export.

This is the older-generation NirSoft framework, not the modern one. The binary is ANSI
(CreateWindowExA, LoadStringA, msvcrt), so the export layer writes single-byte text with
no byte-order mark and the XML carries encoding="ISO-8859-1". The recreation itself is
Unicode internally and reproduces the ANSI on-disk export bytes.

## Capture backend (determined from the binary)

SmartSniff supports four capture methods, selected in Capture Options and stored as
CaptureMethod (1 Raw Sockets, 2 WinPcap, 3 Network Monitor, 4 Network Monitor 3):

- Raw Sockets through WS2_32 (socket, WSAIoctl, recv).
- WinPcap or Npcap through wpcap.dll, loaded dynamically.
- Microsoft Network Monitor through NmApi.dll, loaded dynamically.

The WinPcap path is the one the task asks to reproduce. The loader is `sub_1400219FC`.
It builds the path `<system32>\Npcap\wpcap.dll`, and if that file exists it sets the
current directory to `<system32>\Npcap`, loads the full path, then restores the directory.
Otherwise it loads `wpcap.dll` from the search path. After the module loads it resolves 15
entry points with GetProcAddress (pcap_findalldevs_ex, pcap_freealldevs, pcap_open,
pcap_close, pcap_next_ex, pcap_loop, pcap_compile, pcap_setfilter, pcap_freecode,
pcap_next, pcap_findalldevs, pcap_open_live, pcap_open_offline, pcap_dispatch) and
verifies the required subset is present before treating the driver as usable.

`src/smsniff/PcapLoader.h` reproduces this exactly, with the pcap handle, header and
function signatures declared inline so the project builds with no Npcap SDK. With no
driver installed LoadLibraryA returns null and the loader reports unavailable, so the
window still opens and the Capture Options dialog reports the driver as not installed.

## Security boundary

This build performs no live capture of real traffic. The pcap loader resolves entry
points and reports availability only. It never opens an adapter and never pumps packets.
Start Capture reports the backend state and points at the file-load path. The working
data path is Load Packets Data From File, which reads an `.ssp` capture file. The parser
was validated against hand-crafted synthetic packet bytes only, through a `/selftest`
mode. No real captured data was logged, screenshot or transmitted. The screenshot is
cropped to the top strip (title, menu and toolbar) and shows no packet data.

## The .ssp file format (verified against the loader)

Main header, from `sub_140002BDC`:

- offset 0, 8 bytes, signature "SMSNF100" or "SMSNF200". The numeric part parses with atoi.
- offset 8, 2 bytes, main header size (currently 4).
- offset 10, that many bytes. The first 4 are the capture machine IP as a raw network-order
  dword.

Per-packet record, from `sub_140002FFC`:

- 2 bytes, per-packet header size (currently 0x18 = 24).
- 24 header bytes: a 4-byte received length, an 8-byte FILETIME, a 6-byte source MAC and a
  6-byte destination MAC.
- then the received-length bytes of the IP packet itself, starting at the IP header. The
  MAC addresses are stored separately because a raw-socket capture has no Ethernet frame.

`src/smsniff/Packet.cpp` reads and writes this format and round-trips it in the self-test.

## Packet and connection model (verified against the IP parser)

`sub_140005158` parses one IPv4 packet into a connection key. Offsets recovered:

- protocol at IP+9 (1 ICMP, 6 TCP, 17 UDP).
- IP total length is the big-endian 16-bit field at IP+2.
- source IP at IP+12, destination IP at IP+16.
- direction: when the destination IP equals the capture machine IP the packet is incoming,
  so the local endpoint is the destination and the remote endpoint is the source.
  Otherwise it is outgoing and the local endpoint is the source.
- TCP ports at IP+IHL, header length is IHL plus four times the TCP data-offset nibble, and
  the TCP sequence number is the big-endian dword at TCP+4.
- UDP ports at IP+IHL, header length is IHL plus 8.
- ICMP carries no ports, header length is IHL plus 8.
- the local MAC follows the local side and the remote MAC the remote side.

`sub_1400057B4` aggregates packets into a connection, summing packet count, total size (IP
total length) and data size (total minus header length), tracking the capture time, the
last packet time and the outgoing subtotal, and computing data speed as data size times
1000 divided by elapsed milliseconds times 1024.

`BuildConnections` reproduces all of this. The connection key is protocol, local IP, local
port, remote IP and remote port, so both directions of a flow land in one row. Data speed
and duration come from the first-packet and last-packet FILETIME span rather than live tick
counts, which is a small accuracy improvement for file-loaded data.

## Columns

23 upper-pane columns, from STRINGTABLE 1001..1023: Index, Protocol, Local Address, Remote
Address, Local Port, Remote Port, Packets, Total Size, Capture Time, Data Size, Service
Name, Local Host, Remote Host, ProcessID, Process Filename, Local MAC Address, Remote MAC
Address, Local IP Country, Remote IP Country, Last Packet Time, Data Speed, Process User,
Duration. The array index is the field index, so the saved Columns blob stays compatible.

Columns that need data this build does not have degrade to empty: Local Host and Remote
Host need DNS resolution, ProcessID, Process Filename and Process User need a live capture,
and the two IP Country columns need an IP location file. Service Name uses a small
well-known-ports table rather than the system services lookup, which keeps the parser
self-contained and deterministic.

## Export (`/s*`)

The command-line handler `sub_14001AA58` accepts /stext, /stab, /stabular, /shtml,
/sverhtml and /sxml. This build has no /scomma, although the Save dialog still offers the
comma format as filter 7 (STRINGTABLE 501..507). `src/smsniff/Export.cpp` reproduces the
old-generation ANSI layer. The format strings were taken from the binary:

- HTML preamble `<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">` then
  `<html><head>...<title>Packets Summary</title></head>` and `<h3>Packets Summary</h3>`.
- the credit line `<br><h4>Created by using <a href="http://www.nirsoft.net/"
  target="newwin">SmartSniff</a></h4><p>`.
- table open `<table border="1" cellpadding="5"><tr>` with `<th>` headers and
  `<td bgcolor=#FFFFFF nowrap>` row cells.
- vertical rows `<tr><td nowrap><b>Title</b><td bgcolor=#FFFFFF>value`.
- XML declaration `<?xml version="1.0"  encoding="ISO-8859-1" ?>` with the two spaces the
  original emits, then a root named for the title and one `<item>` per row.

Output is single-byte text, CRLF line ends, no byte-order mark.

## Window geometry

Main window class "SmartSniff", menu 102, accelerators 103. `WinMain` creates it with
`CreateWindowExA` style 0xCF0000 (WS_OVERLAPPEDWINDOW), X and Y both CW_USEDEFAULT and a
fixed 640 by 480 size. The recreation passes a rect of CW_USEDEFAULT origin and 640 by 480
size to CreateEx so the first launch matches. Saved placement overrides later launches.

The toolbar is 12 buttons over bitmap 104 (180 by 16, 11 icons). The light-mode fix from
the guide is applied: PinToLightMode before InitCommonControls, PinToolbarLight after the
toolbar is created, and an OnEraseBkgnd handler that fills the frame with the button-face
colour.

## Config

A SmartSniff.cfg INI next to the executable, section [General], read by `sub_140015E50`
and `sub_1400022E8`. `src/smsniff/Config.h` loads and saves the view and display options,
the protocol filter, the export settings, the capture method and adapter names, the capture
and display filters, WinPos as a hex blob, Columns as a width-and-index blob and Sort as a
signed index, matching the original value names and defaults.

## Build

- MSBuild VS2022 v143, x64, Debug and Release. Both configurations build clean.
- Release flags come from the repo `Directory.Build.targets`, not the project.
- The .rc carries the original requireAdministrator manifest, so the linker manifest is off.
- Debug output `src/smsniff/x64/Debug/smsniff.exe`, Release `src/smsniff/x64/Release/smsniff.exe`.

## Validation

- Parser self-check: 24 of 24 checks pass. Direct IP parsing of hand-crafted TCP, UDP and
  ICMP vectors, a non-IPv4 rejection, an `.ssp` write-then-read round trip, and connection
  grouping with correct endpoints, packet and byte totals, direction counts and service
  name. Run with `smsniff.exe /selftest <report>`.
- Export format: the rebuild's headless `/shtml`, `/sxml`, `/stext` and `/stab` on a
  synthetic `.ssp` reproduce the binary's format strings exactly (DOCTYPE, the nirsoft
  credit line, the `<td bgcolor=#FFFFFF nowrap>` cells and the ISO-8859-1 XML declaration),
  with correct data (a TCP flow 192.168.1.10:49152 to 93.184.216.34:80, 2 packets, total
  151 bytes, data 71 bytes, service http).
- Launch geometry: config cleared, both launched elevated. Original 640 by 480, rebuild
  640 by 480. Position is CW_USEDEFAULT for both, so the OS cascade chooses it and it
  varies by launch order.
- Light toolbar: the rebuild's ToolbarWindow32 empty area reads 240,240,240 at every
  sampled point. Screenshot of the top strip at
  `C:\Users\itag7\AppData\Local\Temp\claude\09ef33d2fe7ff07f\52a65869-da3e-48e8-af92-e3d78e1b4017\scratchpad\smsniff_toolbar_strip.png`.

## What did not match or was not reproduced

- Live capture is intentionally absent for the security reason above. The original captures
  through raw sockets, WinPcap or Network Monitor. The recreation reproduces the backend
  detection and the wpcap load path, and views packets from an `.ssp` file.
- The original cannot export headlessly with no loaded packets, so no byte-for-byte
  comparison of a populated export against the original was possible. The export was
  validated by matching the binary's own format strings and by rendering the synthetic data.
  Loading an `.ssp` argument in headless export mode is a convenience this recreation adds.
- The HTML header row bgcolor and font color come from the list's configured colours in the
  original. Without a populated run those defaults could not be read, so the recreation uses
  a plain `<tr>` header and white row cells, which is the common default.
- Columns needing DNS, process information or an IP location file render empty. All columns
  are shown by default rather than the original's first-run subset.
- The lower pane uses a read-only fixed-pitch edit for the hex and ascii views rather than
  the original's RichEdit, which avoids the riched20 dependency.

## Runtime validation

Date 2026-11-17, against the real Npcap now on this machine (npcap service running,
`C:\Windows\System32\Npcap\wpcap.dll` present). The agent session is not elevated, so the
requireAdministrator GUI ran through `.tools\launch-elevated.ps1`. No real adapter traffic
was captured, logged or shown.

### wpcap.dll dynamic load resolves against the real Npcap
A probe compiled over the tool's own `PcapLoader.h` ran `PcapApi::Load()` against the
installed driver:

```
Load() returned        : 1
api.available          : 1
module path            : C:\WINDOWS\system32\Npcap\wpcap.dll
resolved entry points  : pcap_findalldevs, pcap_findalldevs_ex, pcap_open_live, pcap_open,
                         pcap_compile, pcap_setfilter, pcap_next_ex, pcap_loop,
                         pcap_dispatch, pcap_close, pcap_open_offline (all non-null)
```

The module resolved through the loader's own System32\Npcap path branch, and every entry
point in the required subset came back, so availability flipped from the offline run's "not
installed" to usable. This is the main step up from the offline validation.

### Adapter list populates in Capture Options
The Capture Options dialog fills its adapter list from `GetAdaptersInfo`, the same call the
probe ran. Nine rows came back (Realtek NIC, VMware, Hyper-V, Tailscale and so on), each
with its IP and description, so the list populates.

One honest gap worth recording: the recreation enumerates through `GetAdaptersInfo` (the OS
IP adapter list), not `pcap_findalldevs`. The Npcap loopback capture pseudo-device is not an
IP adapter, so it is not among the rows and 127.0.0.1 does not appear. Picking the loopback
device for capture is not reachable from this dialog. The original keys its own list off the
pcap device enumeration.

### Start Capture
By design this build performs no live capture (see the Security boundary section). The Start
Capture handler reports the backend state and points at the file-load path rather than
opening an adapter. With the WinPcap method selected and the driver now usable it shows the
"Live capture is disabled in this build" information box. It does not error or crash. A live
connection row from a real adapter is therefore not something this build can produce.

### Connection row end to end (loopback, synthetic)
To drive the row path safely, a benign `.ssp` was built holding two invented loopback
packets, `127.0.0.1:54321` to `127.0.0.1:80` TCP, with payloads `GET / HTTP/1.0` and
`Host: localhost`. The shipped Release binary run headlessly with `smsniff.exe /stext
out.txt loopback.ssp` produced one connection row:

```
Index             : 1
Protocol          : TCP
Local Address     : 127.0.0.1
Remote Address    : 127.0.0.1
Local Port        : 80
Remote Port       : 54,321
Packets           : 2
Total Size        : 115
Data Size         : 35
Local MAC Address : 02-00-00-00-00-02
Remote MAC Address: 02-00-00-00-00-01
Duration          : 0:00:03
```

So the real `.ssp` parser, the `BuildConnections` grouping and the export render a loopback
conversation correctly. Both endpoints read 127.0.0.1 and the two packets aggregate into one
row with the byte totals and duration computed.

### Shipped-binary self-check and GUI launch
`smsniff.exe /selftest` on the Release binary passed all 24 checks (RESULT PASS), run
elevated. The Release GUI launched elevated with `/noloadsettings`, called the real wpcap
load at startup, created its window (class `SmartSniff`, 640 by 480, matching the original
geometry), stayed responsive and did not crash. A cropped top strip confirmed the light
toolbar band with colour icons.

### Not validated
Live capture of real adapter traffic is out of scope by design and by the security rule, so
no real packets were captured. The loopback row came from invented bytes, not from the wire.

## Fidelity and HRC pass

Date 2026-11-17. A config-storage fidelity review against the original plus a light
HRC v1.1 touch on the one file changed. No behaviour change beyond the config byte
format.

### Config storage, one fix
The original keeps SmartSniff.cfg next to the exe under section [General], confirmed in
the save routine sub_1400022E8 and the readers around sub_140015F78, both anchored on the
aGeneral string. The value names and defaults already matched. The gap was the binary
value encoding. The shared NirSoft config layer writes WinPos and Columns as uppercase
hex byte pairs joined by a single space with no trailing space, for example
"2C 00 00 00", produced by the serializer sub_14000ABF0 with the format "%2.2X ". The
recreation wrote the same bytes as one unspaced run, so the original could not read the
recreation's blobs and the recreation stopped reading the original's at the first space.

Fixed in Config.h. toHex now joins the pairs with a single space and no trailing space,
and fromHex skips the separators, so it still reads any older unspaced value too. The
Columns record stays width (LE WORD) then internal index (LE WORD), and Sort stays a
signed decimal, both already correct.

### HRC edits
In the same Config.h the C-style casts in the profile getters and the column packing
became static_cast, and toHex and fromHex gained [[nodiscard]]. The unscoped enums
ColKind and Proto were left as is. Each is used across several files and some values are
stored or compared as integers, so an enum class switch would ripple and risk a behaviour
change, which the surgical rule rules out.

### Rebuild and re-validation
- Debug and Release x64 rebuilt clean with VS 2022 v143, only the usual /Ob2 to /Ob3
  override warning.
- Config round-trip over the tool's own Config.h. A seeded config saved to a .cfg and
  reloaded round-trips every field, and the raw file shows [General] with space-separated
  WinPos and Columns and Sort=-3. PASS.
- Parser self-check on the Release binary, run elevated. 24 of 24 PASS.
- Export is unchanged, since Config.h was the only file touched, and the format strings
  stay intact.
- First-launch geometry 640 by 480, matching the original.
- Light toolbar. The elevated rebuild's ToolbarWindow32 empty area reads 240,240,240 at
  four points and the cropped strip shows the colour icons on a light band.

## Header and cpp split

Date 2026-11-18. Behaviour-preserving code motion plus the HRC clang-format pass, with
no change to any other tool. The CMainFrame window-class method bodies moved out of
MainFrame.h into a new MainFrame.cpp, so the header now carries only the class shape:
members, the message map, DECLARE_FRAME_WND_CLASS, the WTL macros, the static kSplitBar
constant and the method declarations. The one trivial accessor disp() stays inline. The
golden template was src/WinObj (commit 4a3adc0).

- MainFrame.h 788 to 222 lines. New MainFrame.cpp is 1083 lines, both after clang-format.
  No other header in src/smsniff is over ~600 lines, so only MainFrame needed the move.
- Bodies moved verbatim in the same order, with override dropped on the out-of-line
  PreTranslateMessage definition and kept on the declaration. WTL handler signatures keep
  their trailing BOOL&, the OnEraseBkgnd to FillFrameBtnFace call and the PinToolbarLight
  and PinToLightMode calls are byte-identical. The message map, the custom m_uFindMsg
  branch and the function-local FilterDlg dialog moved unchanged.
- The new unit includes MainFrame.h first, so ATL pulls in the Win32 headers before the
  bodies and no include-order fix was needed. MainFrame.cpp was added to the
  smsniff.vcxproj ClCompile group. clang-format ran over src/smsniff/*.cpp and *.h with
  the repo .clang-format.
- Debug and Release x64 rebuilt clean with VS 2022 v143 and /t:Rebuild, only the usual
  /Ob2 to /Ob3 override warning.

### Re-validation (elevated, no capture)
The Release binary launched elevated through .tools/launch-elevated.ps1 with
/noloadsettings and the config cleared. No capture ran and no file was loaded, so the
upper list and lower pane held no packet or credential data. The sampling ran elevated
through the broker so it could read the requireAdministrator window.

- Window class SmartSniff, outer size 640 by 480, matching the original geometry.
- All four child panes render: SysListView32 upper list, Edit lower pane, ToolbarWindow32
  toolbar and msctls_statusbar32 status bar. The status bar reads "0 TCP/IP conversations"
  and "0 Packets". The menu is File, Edit, View, Options, Help and the toolbar shows its
  colour icons.
- The ToolbarWindow32 empty area reads 240,240,240 at five sampled points, so the
  dark-toolbar fix survives the split. The toolbar child was found with
  FindWindowExW(frame, 0, "ToolbarWindow32", null).
- Full-window screenshot, empty of any packet data, at
  `...\52a65869-da3e-48e8-af92-e3d78e1b4017\scratchpad\smsniff_toolbar_strip.png`.
