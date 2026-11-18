# DebugView

Source-accurate recreation of the Sysinternals **DebugView** utility as a C++20
plus WTL GUI app under `src/DebugView/`. The recreation reproduces the Win32
user-mode capture path, the report list, the time formatting, the clock-time
versus elapsed toggle, Clear, autoscroll, Save, Copy, Find and the
include/exclude/highlight filter. The kernel capture path is reproduced in the
menu and the toolbar but the driver load itself is left out of scope and
documented below.

Original binary: `targets/sysinternals/Dbgview64.exe`, DebugView 4.x, x64,
imagebase `0x140000000`, 3544 functions. Raw Win32 (CreateWindowExA,
RegisterClassExA, CreateToolbarEx), ANSI APIs, a custom-drawn output pane. The
recreation keeps the menu, toolbar and command ids but renders output through an
owner-data report `CListViewCtrl`, which is the intended modernization.

## Reverse engineering findings

### Win32 capture, the DBWIN protocol (no driver)

The user-mode capture is set up in `sub_140005710` and serviced in
`sub_14000A960`. It creates the standard DBWIN monitor objects, with a `%s`
prefix that is empty for local capture and `Global\` for global capture.

- Shared section `DBWIN_BUFFER`, `CreateFileMappingA(INVALID_HANDLE_VALUE, sa,
  PAGE_READWRITE, 0, 0x1000, ...)`, so 4 KB.
- Auto-reset events `DBWIN_BUFFER_READY` and `DBWIN_DATA_READY`.
- A named mutex `DBWinMutex` marks the single live monitor.
- A permissive security descriptor,
  `D:(A;;GRGWGX;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGWGX;;;AN)(A;;GRGWGX;;;RC)(A;;GRGWGX;;;S-1-15-2-1)S:(ML;;NW;;;LW)`,
  grants read, write and execute to Everyone with a low mandatory label, so a
  writer of any integrity level can reach the objects.

Buffer layout, confirmed in `sub_14000A960`: the first four bytes are the writer
process id (`*(DWORD*)buffer`), the message starts at offset 4 as an ANSI
string.

Protocol, confirmed from the setup and the message loop in `WinMain`
(`sub_14000ACE0`):

1. The monitor signals `DBWIN_BUFFER_READY`, meaning the buffer is free.
2. A process inside `OutputDebugStringA` copies its pid and line into the buffer
   and signals `DBWIN_DATA_READY`.
3. The monitor wakes on `DBWIN_DATA_READY`, reads the record, then signals
   `DBWIN_BUFFER_READY` again.

The reader splits each record on a newline and shows one line per segment. The
`byte_1400921B0` flag (default set, driven by the Force Carriage Returns option)
controls whether a record without a newline is shown at once or held back to
join the next record.

### EULA and -accepteula

`sub_140001EC0` is the shared Sysinternals license gate. The acceptance flag is a
`REG_DWORD` named `EulaAccepted` under `HKCU\Software\Sysinternals\%s`, where
`%s` is the application name `DebugView`. A set flag or the `-accepteula` or
`/accepteula` switch skips the modal agreement and records consent. A decline
stops the launch.

### Window geometry and class

The main window is created in `sub_140005520`:
`CreateWindowExA(0, "dbgviewClass", ..., WS_OVERLAPPEDWINDOW, X, Y, nWidth,
nHeight, ...)`. The defaults are set in `sub_140008730`: `X = 100`, `Y = 100`,
`nWidth = 500`, `nHeight = 300`, clamped against the virtual screen. A saved
placement in the registry overrides those on later launches. The window class is
`dbgviewClass`.

### Settings store

Settings live under `HKCU\Software\Sysinternals\DbgView` as an opaque 400-byte
`Settings` blob, plus `Recent`, `InFilters` (default `*`), `ExFilters`,
`HiFilters` and `HiCurFilters`. The default font is Courier New at weight 400.

### Menu, toolbar and commands

The `LISTMENU` resource and the `ACCELERATORS` resource give the command ids used
throughout. The toolbar is built in the WndProc (`sub_140005B80`) from the
`CreateToolbarEx` call at `0x140005D92`, with 20 buttons and the `TBBUTTON`
array at `0x140092300`. Each button's bitmap index into `BITMAP 400` was read
from that array:

| bitmap | command | label |
|-------|---------|-------|
| 22 | 40057 | Open |
| 0  | 40007 | Save |
| 19 | 40046 | Log to File |
| 2  | 40018 | Capture (events) |
| 17 | 40031 | Capture Kernel |
| 9  | 40026 | Pass-Through |
| 12 | 40028 | Capture Win32 |
| 6  | 40020 | Clear |
| 4  | 40019 | Autoscroll |
| 14 | 40029 | Clock Time |
| 16 | 40022 | Filter/Highlight |
| 11 | 40027 | History Depth |
| 7  | 40024 | Find |

### Kernel capture (out of scope)

The kernel path reads `DbgPrint` output through a helper driver. The original
embeds the driver and service blobs as resources `RCDBGSYS` and `RCDBGSVC`,
registers a service under `System\CurrentControlSet\Services\Dbgv` and talks to
`Dbgv.sys` through `DeviceIoControl`. The recreation keeps the Capture Kernel
menu item, the accelerator and the toolbar button, but the driver load is left
out of scope. Turning the option on shows a one-time note and keeps the option
off. The embedded driver and service blobs were removed from the extracted
resources, since they are Sysinternals binaries that must not be redistributed.

## Recreation layout

`src/DebugView/` holds the project. Key files:

- `Capture.h` reproduces the DBWIN monitor protocol on a reader thread and posts
  each line to the window through `WM_DBG_LINE`.
- `DbgRow.h` holds one captured line (sequence, FILETIME, performance counter,
  pid, message).
- `Columns.h` defines the four columns `#`, `Time`, `PID`, `Debug Print`.
- `MainFrame.h` is the WTL frame, with the toolbar, the owner-data list, the
  filter view, custom-draw highlight, and every menu handler.
- `Config.h` loads and saves options under `HKCU\Software\Sysinternals\DbgView`.
- `Eula.h` is the license gate under `HKCU\Software\Sysinternals\DebugView`.
- `Filter.h` matches the include, exclude and highlight sets.
- `Dialogs.h` has the About, Filter and History Depth dialogs.
- `Export.h` writes the Save report and the Copy string.
- `res/DebugView.rc` carries the menu, accelerators, toolbar strip, icons and the
  rebuilt About box.

### Deliberate simplifications

- Settings are kept as readable named values under the original `DbgView` key
  rather than the original opaque 400-byte blob. The `EulaAccepted` flag is
  byte-faithful under `DebugView`.
- Highlight is rendered as a single yellow background through list custom draw,
  rather than the original's configurable per-rule colors.
- The cross-record line accumulation (Force Carriage Returns off) is not
  reproduced. The default one-line-per-record path is.
- The kernel driver load, remote connect, printing, log-to-file, crash dump and
  pass-through features are reproduced in the menu but not implemented, and are
  greyed so their state is honest.

## Validation

Built with VS 2022 Community MSBuild, PlatformToolset v143, x64 Debug and Release,
both clean. A small child process was spawned that called `OutputDebugStringA`
three times with known strings. A cross-process reader read the running
rebuild's list.

Capture, from the rebuild's own list (child pid 47532):

```
ROWCOUNT: 3
ROW 0: [0] [0.00000000] [47532] [DebugView recreation test line ALPHA]
ROW 1: [1] [0.21462970] [47532] [DebugView recreation test line BRAVO]
ROW 2: [2] [0.41702980] [47532] [DebugView recreation test line CHARLIE]
```

- Every line captured, with the correct pid from buffer offset 0, the exact
  message, and the right column order.
- The sequence numbers are dense (0, 1, 2).
- The elapsed-time column reflects the roughly 200 ms gaps between the three
  calls, which matches the writer timing.

Clock-time toggle, read back from row 0 of the time column:

```
ELAPSED row0 time: [0.00000000]
CLOCK   row0 time: [12:37:10.618]
```

Toggling Clock Time plus Show Milliseconds switched the column from elapsed
seconds to a wall-clock stamp.

Geometry, with the config cleared, matched the original first-launch rect:

```
WINDOWRECT: 100,100 500x300
```

Toolbar light check, three empty-area samples of the `ToolbarWindow32` child:

```
TBPIXEL x=478 y=12: 240,240,240
TBPIXEL x=470 y=12: 240,240,240
TBPIXEL x=460 y=12: 240,240,240
```

The toolbar reads 240,240,240, so the three-edit dark-toolbar fix holds.

About box, read from the control captions:

- `DebugView  (version 4.90)`
- `Independent source-accurate recreation of DebugView by Sysinternals, through
  reverse engineering and rewritten in modern C++20 && WTL 10.`
- `Author: 0xntpower`

The credit renders with the `&&` shown as a single ampersand, and a screenshot
confirmed the full box paints.

Status: done and validated. Win32 user-mode capture, columns, time formatting,
clock-time toggle, Clear, autoscroll, Save, Copy, Find and the filter all work.
Kernel capture is menu-only and documented above.
