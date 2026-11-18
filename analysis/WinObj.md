# WinObj (Sysinternals) recreation

Source-accurate C++20 plus WTL recreation of the Sysinternals **WinObj** Object
Manager namespace browser. Original binary `targets/sysinternals/Winobj64.exe`,
version 3.14, an MFC x64 application. SHA-256
`e2a2a818ab71c388fa2e4d4af6a1abbeb32c9fcbaa22ec7e7c0cc8a044639662`.

Project at `src/WinObj/`. Built with VS 2022 Community MSBuild, v143, stdcpp20,
Unicode, static CRT, x64 Debug and Release. Not added to any solution per the
request.

## What it does

Browses the Windows Object Manager namespace. A tree on the left holds the
object directories rooted at `\`, and a list on the right shows each object's
Name, Type and, for symbolic links, the resolved target. Double-clicking a
directory in the list navigates into it. A Properties sheet shows the object's
details and security.

## Reverse engineering findings

Engines: IDA idalib MCP as the primary decompiler, cross-checked against the
rendered listing. Session opened and closed cleanly.

- **Native namespace walk.** The native routines have no import stubs and are
  resolved from ntdll by name at run time. Confirmed present in `.rdata`:
  `NtOpenDirectoryObject`, `NtQueryDirectoryObject`, `NtOpenSymbolicLinkObject`,
  `NtQuerySymbolicLinkObject`. Each child record is an
  `OBJECT_DIRECTORY_INFORMATION` carrying the child Name and its object
  TypeName, which drives the Type column. The binary carries a Detours-style
  trampoline layer around these calls, visible as the `.detourc` / `.detourd`
  segments and the triplicated resolver stubs.
- **EULA gate** at `sub_1400032D0`. It formats `Software\Sysinternals\%s` with
  the app name, reads the per-app flag, and on acceptance writes
  `HKCU\Software\Sysinternals\WinObj`, value `EulaAccepted`, `REG_DWORD` 1. It
  honours `/accepteula` and falls back to a console Y/N prompt on Nano and
  IoT hosts, otherwise a RichEdit License Agreement dialog. The recreation
  reproduces the exact key and value and the `/accepteula` switch.
- **Main window** at `sub_140031EF0`. It creates a `ToolbarWindow32` inside a
  `ReBarWindow32`, a quick-find band from dialog 103, a `SysTreeView32`, a list
  view, and a status bar. It detects elevation: when not elevated it inserts a
  `Run As Administrator` item, and when elevated it appends ` (Administrator)`
  to the window title. The recreation reproduces the title suffix.
- **Properties** is a property sheet. The Details page is dialog 107 with Name,
  Type, a Permanent check box, Reference and Handle counts, Paged and Non-Paged
  quota, and a symbolic-link group. The Security page comes from ACLUI's
  `CreateSecurityPage` (import confirmed), driven by an `ISecurityInformation`
  over the object's security descriptor.
- **List columns** are Name, Type, Symbolic Link Target. The Search dialog (226)
  uses Type, Name, Directory, Symbolic Link Target.
- **Resources** reused through Resource Hacker: menu 128, context menus 220,
  dialogs 100 / 103 / 107 / 226, accelerators 128, the icon set, the version
  block and the string table.

### Manifest note (deliberate deviation)

The real `Winobj64.exe` manifest is **asInvoker**, not requireAdministrator. The
binary runs unelevated and offers elevation from inside through the Run As
Administrator command. The request asked for a requireAdministrator manifest and
for launching through the elevated launcher, so the recreation ships a
**requireAdministrator** manifest. This is the one intentional departure from the
original binary and is recorded here. Flip the manifest to asInvoker to match the
original byte-for-byte if that is preferred.

## Implementation

- `NtObjMgr.h/.cpp` -- the namespace walker. Dynamic ntdll resolution,
  `EnumerateDirectory`, `ResolveSymlink`, a per-type open dispatch used by both
  the Details counts (`NtQueryObject` ObjectBasicInformation) and the Security
  page (`NtQuerySecurityObject`), and an elevation check.
- `MainFrame.h` -- `CFrameWindowImpl` frame with tree, list, a draggable
  vertical splitter, toolbar, status bar and a quick-find band. Full tree build
  from the namespace root, owner-data list, per-type row icons, column sort,
  copy commands, pause and update-interval timer, always-on-top, font chooser.
- `Dialogs.h` -- the About box and the Properties sheet (Details page plus the
  ACLUI Security page).
- `Security.h` -- the read-only `ISecurityInformation` for the Security page.
- `WinObj.cpp` -- entry point, `/accepteula` handling, the EULA gate, light-mode
  pin before `InitCommonControls`, and the GUI loop.
- The toolbar gets the shared light fix: `PinToolbarLight` after creation, a
  `WM_ERASEBKGND` handler, and `FillFrameBtnFace`.

## Validation

Both the original and the recreation were launched elevated through
`.tools/launch-elevated.ps1` and captured at the same integrity level. Object
counts differ run to run because transient named objects (Jobs, Events, ports)
come and go, so those were normalized out of the comparison.

- **Top-level directory tree matches.** Both show, under `\`: ArcName,
  BaseNamedObjects, Callback, ContainerManager, Device, Driver, DriverStore,
  FileSystem, GLOBAL??, KernelObjects, KnownDlls, KnownDlls32, NLS, ObjectTypes,
  RPC Control, Security, Sessions, UMDFCommunicationPorts, Windows. Identical.
- **Type values match.** For a stable sample the recreation and the original
  agree: ArcName / BaseNamedObjects / Device / GLOBAL?? = Directory, clfs / Fat
  / FatCdrom = Device, CsrSbSyncEvent / EFSInitEvent = Event, the Container_* =
  Job, BindFltPort / CLDMSGPORT / GameFltCommunicationPort = FilterConnectionPort,
  Dfs / DosDevices / DriverData / DriverStores = SymbolicLink.
- **Symbolic links resolve** and match the original: Dfs to `\Device\DfsClient`,
  DosDevices to `\??`, DriverData to `\SystemRoot\System32\Drivers\DriverData`,
  DriverStores to `\DriverStore\Nodes`.
- **Toolbar is light.** The `ToolbarWindow32` child of the recreation reads
  **240,240,240** in its empty area (corner and mid-right samples both). The
  original reads 255,255,255 because its compact toolbar sits in a white rebar
  band. Both are light. The recreation hits the expected button-face value.
- **Geometry matches.** With no saved placement both open at the default size
  1920x1023 and cascade from the default origin.
- **About credit renders** exactly: `Independent source-accurate recreation of
  WinObj by Sysinternals, through reverse engineering and rewritten in modern
  C++20 & WTL 10.` and `Author: 0xntpower`. The ampersand is written doubled in
  the source and renders as a single `&`.
- **Properties Details page** populates from `NtQueryObject`. For the root
  directory it shows Type Directory, Permanent checked, References 32848,
  Handles 1, Paged and Non-Paged 0. The sheet is multi-page with the ACLUI
  Security page added whenever a handle to the object opens. The themed tab
  strip does not render under PrintWindow capture, which is a known capture
  limitation, not a missing page.

## Deliberate simplifications and out of scope

- **Dark theme** from the original's Options menu is omitted. The recreation is
  pinned to light mode, matching the project convention and the toolbar-light
  validation, so the Theme submenu was left out.
- **Auto-refresh** defaults to paused. The Pause command and the Update Interval
  submenu drive a refresh timer, but a full namespace re-walk every interval is
  heavy and flickery, so it starts paused.
- **Search Objects** (dialog 226, a recursive namespace search) is not built as
  a separate window. The Search command opens the Quick Find band, which finds
  within the current directory. The Search dialog resource is kept for later.
- **List row icons** use three stock icons (generic, folder, link) rather than
  the original's per-type icon set. The Type column carries the exact type name.
- **Symbolic-link creation time** is not shown on the Details page. The target
  is resolved and shown. There is no documented by-name query for the time.
- **Basic Information counts** fill only for object types with a clean single
  open routine (Directory, SymbolicLink, Mutant, Event, Semaphore, Timer,
  Section, Job, KeyedEvent). Other types leave the counts blank, matching the
  original's behaviour on objects it cannot open.

## Build

```
MSBuild src\WinObj\WinObj.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\WinObj\WinObj.vcxproj /p:Configuration=Release /p:Platform=x64
```

Both configurations build clean. The only warnings are the `/Ob2` to `/Ob3`
override from the shared `Directory.Build.targets`, shared by every tool.
