# SimpleProgramDebugger -- reverse-engineering notes

Target: `targets/simpleprogramdebugger-x64/SimpleProgramDebugger.exe` -- NirSoft
SimpleProgramDebugger v1.16, a lightweight debugger that launches or attaches to
a program and logs the debug events it produces. **x64 PE, Unicode, not packed.**
md5 `2a5069a4f2fbc1efbcaebaa71a225fb2`, sha256
`9837b28a68ac0ab13a17c27d0cfcd0ab0f6cb67f510ed055ef13cdefed911fe4`. Image base
0x140000000, image size 0x58000, 350 functions. The copyright string is
`Copyright (c) 2014 - 2022 Nir Sofer`. Engine used -- IDA idalib session, plus a
run of the original against a controlled target to recover the exact column text.

## What it does
The tool runs a `WaitForDebugEvent` loop on a background thread and records each
event into a list. It can start a new process under the debugger (`CreateProcessW`
with `DEBUG_PROCESS`, so child processes are debugged too) or attach to a running
one (`DebugActiveProcess`). `SeDebugPrivilege` is enabled at startup. The loop and
the launch/attach code live in `sub_140012C54`, and the per-event record is built
by `sub_140012A34`, which posts it to the window with a private message (0x465).

The loop special-cases the breakpoint exception. When the event is an exception
with code `0x80000003` (EXCEPTION_BREAKPOINT), the loop continues it with
`DBG_CONTINUE` and does not log it. Every other event is continued with
`DBG_EXCEPTION_NOT_HANDLED` and logged. The loop ends when the main target raises
`EXIT_PROCESS`, after which it calls `DebugSetProcessKillOnExit(FALSE)` and
`DebugActiveProcessStop`.

## Columns (string ids 1001..1006)
Six columns, all visible by default in this order. Default widths read from the
original's own list header.

| Index | Id | Title | Width |
|-------|----|-------|-------|
| 0 | 1001 | Event Time | 120 |
| 1 | 1002 | Event Type | 120 |
| 2 | 1003 | Thread ID | 100 |
| 3 | 1004 | Memory Address | 100 |
| 4 | 1005 | Filename | 200 |
| 5 | 1006 | More Information | 200 |

## Per-event column formatting
Recovered from the column builder `sub_1400024CC`, the time formatter
`sub_140002284`, and a live run of the original against a test target.

- **Event Time** is elapsed time since the session start baseline, printed as
  `%.2d:%.2d:%2.2d.%.3d` (hours, minutes, seconds, milliseconds). It is not a wall
  clock. The baseline is captured once when debugging begins.
- **Event Type** is the event name, string ids 1501 Exception, 1502 Create Thread,
  1503 Create Process, 1504 Exit Thread, 1505 Exit Process, 1506 Load DLL, 1507
  Unload Dll, 1508 Debug String, indexed by `dwDebugEventCode`.
- **Thread ID** is `dwThreadId` in decimal.
- **Memory Address** is the exception address for an exception, the thread start
  address for Create Thread, and the DLL base for Load DLL and Unload Dll. It is
  empty for Create Process, Exit Thread, Exit Process and Debug String. On the
  64-bit build it prints as `%8.8x`%8.8x` (high word, backtick, low word). A zero
  address renders as an empty cell.
- **Filename** is the module path for Load DLL and Unload Dll, empty for every
  other event. The Load DLL path is cached by base address so Unload Dll can show
  the same path.
- **More Information** is `Exception Code: 0x%8.8x` for an exception, `Exit Code:
  0x%8.8x` for Exit Thread and Exit Process, and the string itself for a Debug
  String event. It is empty for Create Process, Create Thread, Load DLL and Unload
  Dll. The string ids for the labels are 1601 Exception Code, 1602 Parameters and
  1603 Exit Code.

## Export formats and encodings
Seven formats, confirmed by capturing the original's own output. There is no JSON
format in this tool.

- `/scomma` -- UTF-8 with **no BOM**. Comma-separated, no trailing comma, values
  quoted when they contain a comma, quote or line break. Header line on by default.
- `/stab` -- UTF-16LE with a BOM. Tab-separated with a **trailing tab** after the
  last field. Header line on by default.
- `/stext` -- UTF-16LE with a BOM. Each record is wrapped in a 50-'=' rule, the
  field names are left-justified to width 18 then `: `, and a blank line follows.
- `/stabular` -- UTF-16LE with a BOM, fixed-width columns. Empty input produces the
  BOM only.
- `/shtml` and `/sverhtml` -- UTF-16LE with a BOM. The classic NirSoft
  `border="1" cellpadding="5"` template with an HTML 3.2 doctype and a credit line
  to nirsoft.net. The title and header is `Debug Events List`.
- `/sxml` -- UTF-16LE with a BOM. `<?xml version="1.0" ?>`, root
  `debug_events_list`, each `<item>` with tag names slugged from the column titles.

The `Add Header Line To CSV/Tab-Delimited File` option controls the header line on
the comma and tab formats. This tool has no Save File Encoding option, so the
encodings above are fixed.

## Command-line switches
- `/DebugProcess <pid|name>` -- attach to an existing process by id or by exe name.
- `/StartDebugProcess` -- start a new process and debug it, using the
  `/NewProcess.ProcessPath`, `/NewProcess.Params` and `/NewProcess.StartFolder`
  values, falling back to the saved values.
- `/NewProcess.ProcessPath`, `/NewProcess.Params`, `/NewProcess.StartFolder`.
- Export switches, each taking a filename: `/stext /stab /stabular /shtml
  /sverhtml /sxml /scomma`.
- `/RunAsAdmin` -- relaunch elevated.
- `/deleteregkey` -- the recreation deletes the settings key for this one.
- `/savelangfile` -- present in the binary, not reconstructed.

## Config -- `HKCU\Software\NirSoft\SimpleProgramDebugger`
Value names recovered from the binary: `MarkOddEvenRows`, `ShowGridLines`,
`SaveFilterIndex`, `ShowInfoTip`, `ListViewSortProcess`, `ScrollDownOnNewLine`,
`NewProcess.ProcessPath`, `NewProcess.Params`, `NewProcess.StartFolder`,
`SortOnEveryUpdate`, `TrayIcon`, `StartAsHidden`, `AddExportHeaderLine`, `WinPos`,
`Sort`, `Columns`. The `Columns` blob is an array of (WORD width, WORD columnIndex)
in display order.

## UI (resources)
Main menu 102, context menu 104, tray menu 116, accelerators 103. Dialogs --
Properties 105, Exception 107 (a register and stack dump), About 112, Select a
process 113, Start New Process 114, Column Settings 1096. Toolbar bitmap 110 is a
144x16 24-bit strip of nine 16x16 images, built with `CreateToolbarEx` in
`sub_14000BB88`. The eleven toolbar entries are a leading separator, Attach (image
6), Start New Process (image 8), Detach (image 7), a separator, Save (image 1),
Refresh (image 5), Copy (image 2), Properties (image 3), Find (image 0) and Exit
(image 4). The window class is `SimpleProgramDebugger` and the main window is
created with `CreateWindowExW` passing `CW_USEDEFAULT` for X, Y, width and height
and the `WS_OVERLAPPEDWINDOW` style. The manifest has no requested execution level,
so the tool runs as asInvoker.

## Reconstruction
Clean C++20 plus WTL under `src/SimpleProgramDebugger/`, x64 Unicode, output
`SimpleProgramDebugger.exe`. Files -- `SimpleProgramDebugger.cpp` (WinMain and the
CLI), `Debugger.cpp/.h` (the WaitForDebugEvent loop on a worker thread and the
per-event column formatting), `DebugEvent.h` (the six precomputed cells plus sort
keys), `Columns.h`, `Export.cpp/.h` (seven formats), `Config.h`, `RegKey.h`,
`MainFrame.h`, `Dialogs.h`, `resource.h`, `res/SimpleProgramDebugger.rc`. The
debug thread posts each finished row to the window with a private message, the way
the original posts to its window. The toolbar is built with
`LoadToolbarImageList(IDB_TOOLBAR)` from the shared `common/UiUtil.h`, which keys
the strip corner transparent, so the icons render as colour rather than a dark box.
Links comctl32, shell32, advapi32, comdlg32, ole32, gdi32, version and psapi. The
project uses PlatformToolset v143 with the atlmfc include and library path, the
same arrangement as the other x64 tools in the tree.

## Build environment note
`GetModuleFileNameEx` is not reliably queryable while the target is frozen at a
LOAD_DLL debug event, because the loader list is still being built. The debug
thread resolves the module path after `ContinueDebugEvent` and opens a fresh query
handle to the process. That recovers the loader-list path and its casing for most
modules. For the first modules of a process (ntdll) even that fails, so the code
falls back to `GetFinalPathNameByHandle` on the event file handle, which returns
the correct path in its on-disk casing.

## Validation -- DONE
- Debug and Release both build for **x64**.
- **Export templates**: the recreation's headless `/stext /stab /stabular /shtml
  /sverhtml /sxml /scomma` are **byte-for-byte identical** to the original's for the
  empty data set, across all seven formats. This pins the structure, headers,
  delimiters, encodings and BOM handling exactly.
- **Debug events**: a test program that loads a DLL, raises a caught exception and
  calls `OutputDebugString` (both wide and ANSI) was debugged under the original
  and the rebuild, and both list views were read back. After normalizing the
  volatile fields (event time, thread id, memory addresses), the deterministic
  events match exactly -- the five debug strings, the Load DLL and Unload Dll of the
  test DLL with the module path, the exception with `Exception Code: 0xe0001234`,
  and the Exit Thread and Exit Process with `Exit Code: 0x00000007`.
- **Export rows**: a self-check feeds synthetic rows through the report builder and
  asserts the text (50-'=' rule, width-18 field names), tab (trailing tab), comma
  (no trailing comma) and xml (slugged tags) row formats against the captured
  originals. It passes.
- **GUI**: launches, debugs the test program through the WaitForDebugEvent loop,
  shows the six columns in order, and the toolbar shows colour icons on a light
  flat bar rather than a dark box. Screenshot at
  `analysis/SimpleProgramDebugger-gui.png`.
- **Launch geometry**: with the config cleared, the original and the rebuild both
  create a `CW_USEDEFAULT` window of the same size (1920x1023 on this display) with
  the same show state (SW_SHOWNORMAL). The positions differ only by the system
  window-cascade stagger, which the original itself varies from launch to launch.

## Deviations
- For the first modules of a debugged process (ntdll), the Filename cell uses the
  on-disk casing from `GetFinalPathNameByHandle` rather than the loader-list casing
  the original shows, because the loader list is not queryable that early. The path
  is correct, only the letter case of the system folder differs. Later modules use
  the loader-list casing and match.
- The system DLL loads and child processes that appear in a debug session depend on
  the machine and on timing, so those rows are environmental rather than fixed.
- The Exception popup dialog (resource 107, a register and stack dump) is kept in
  the resources but not wired, since it is a secondary prompt rather than part of
  the event logging and export core.
- Exception parameters (the 1602 label) are not reproduced in detail. The common
  case shows only the exception code.
- The tray icon, Start As Hidden and Sort On Every Update options are carried in
  the settings and partly wired. `/savelangfile` is not reconstructed and
  `/RunAsAdmin` relaunches through `ShellExecute` runas.
- Headless export writes the current list, which is empty in a headless run, so the
  report carries only its header. This matches the original.

## Fidelity and HRC pass

Config storage finding and fix. The original imports no registry API at all, so it cannot use
the registry for settings. It persists to SimpleProgramDebugger.cfg next to the executable,
section [General], through WritePrivateProfileString. The recreation had stored settings
under HKCU\Software\NirSoft\SimpleProgramDebugger, which was wrong. It now writes the .cfg
file, section [General], with the matching value names. The value ListViewSortProcess, which
the original writes and the recreation had omitted, was added with its default of 0, so the
value name set now matches the original one for one. Blobs are encoded as space separated
uppercase hex bytes, and the Columns blob is byte identical to the original.

HRC v1.1 light touch. Config.h was rewritten for the .cfg store with static_cast, nullptr,
initialized variables and the nodiscard attribute on helpers. ColKind and SpdStatus became
enum class with their enumerator names kept, and the SpdStatus values that cross the window
message boundary are now cast with static_cast to WPARAM. The /deleteregkey handler now
removes the .cfg file. Two C style casts in the entry point and the frame became static_cast.

Rebuild and re-validation. Debug and Release x64 rebuilt clean with v143. A launch and close
round trip confirmed the .cfg carries the original value names including ListViewSortProcess,
with the Columns blob byte identical to the original and no registry key created. The /scomma,
/stab, /sxml and /shtml exports are byte identical to the original. The ToolbarWindow32 empty
area reads 240,240,240 and the window resolves to 1920 by 1023 with showCmd normal.
