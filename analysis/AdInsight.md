# AdInsight (Insight for Active Directory)

Source-accurate C++20 plus WTL recreation of the Sysinternals AdInsight utility,
built as a focused core subset. Original binary `targets/sysinternals/ADInsight64.exe`
(internal name "Insight for Active Directory", version 1.2.0.0).

AdInsight captures the LDAP client API calls that processes make, by injecting a
hook DLL that intercepts the `wldap32.dll` LDAP functions and reports each call
back to the GUI. The main window is a report list of captured calls over a
detail pane.

## Reverse engineering findings (IDA idalib primary, from `ADInsight64.exe`)

Entry and bootstrap:
- `WinMain` loads `riched20.dll`, calls `WSAStartup`, `InitCommonControlsEx(0x3FFF)`,
  enforces a single instance by counting running `ADInsight.EXE` processes, loads
  26 animation bitmaps named `Anim1`..`Anim26` (162x60) for the capture animation,
  registers the main window class under a runtime GUID name with menu `LISTMENU`,
  accelerators `ACCELERATORS` and icon `APPICON`, plus helper classes
  `ChicoSplitterWClass`, `ChicoRelatedSplitterWClass` and `BALLOON`.
- The main window is created with `CreateWindowExW` style `0x10CF0000`
  (`WS_VISIBLE` plus `WS_OVERLAPPEDWINDOW`), window name `ADInsight`, at the rect
  loaded from the config (default system placement on first run).
- Command-line switches recovered: `-q`, `-o` (turn off capture), `-t`, `-run32`,
  `-s`, `-e`, `-d`, `-fps`, `-f <TraceFile>`, `-fi <IncludeString>`,
  `-fe <ExcludeString>`, `-?` (help), plus `-accepteula` and `/accepteula`.

EULA and registry (function `sub_140002F60`):
- The license key is formed as `Software\Sysinternals\%s` with the app name
  `ADInsight`, so acceptance is stored at
  `HKCU\Software\Sysinternals\ADInsight`, value `EulaAccepted` (REG_DWORD 1). A
  suite-level `HKCU\Software\Sysinternals\EulaAccepted` is also honoured. The
  config save path uses both `Software\Sysinternals\AdInsight` and
  `Software\Sysinternals\ADInsight` spellings, which refer to the same key because
  the registry is case insensitive.

Hook DLL extraction and interface (function `sub_14000F270`):
- The hook DLL is embedded as a resource of type `BINRES`, id `1170`.
- On the first capture the GUI builds `<temp>\ADInsightDll.dll`, deletes any old
  copy, extracts the resource with `FindResourceW` and `LockResource`, writes it,
  then calls `MoveFileExW(..., MOVEFILE_DELAY_UNTIL_REBOOT)` to remove it on the
  next reboot, and `LoadLibrary`s it.
- The GUI resolves four exports: `AdmonTraceInit`, `AdmonMonitorStart`,
  `AdmonMonitorStop` and `AdmonPingDLL`. `AdmonTraceInit` sets up the shared trace
  buffers, `AdmonMonitorStart` and `AdmonMonitorStop` toggle capture, and
  `AdmonPingDLL` is a heartbeat.

Injection:
- The GUI imports `SetWindowsHookExW` and `UnhookWindowsHookEx`. The all-process
  mechanism is a global Windows hook that maps the DLL into processes, so a new or
  running process that uses `wldap32` gets the DLL loaded and its LDAP imports
  intercepted.

IPC (functions `sub_14000EE70` server and the controller window `sub_14000FB20`):
- A named pipe `\\.\pipe\ADInsightPipe` is created by the GUI with
  `CreateNamedPipeW` (`PIPE_ACCESS_DUPLEX` plus `FILE_FLAG_OVERLAPPED`, message
  mode, one instance). The GUI reads records and posts them to the window with
  `PostMessageW(hwnd, 0x402, 0, record)`. Status messages use `0x40C` and `0x40D`.
- The controller window also drives a command and response channel for exclusion
  lists, process info and filtering.

Operation decode (function `sub_140014DC0`, large):
- The GUI formats the captured raw arguments into human-readable operation names,
  decoding `LDAP_SCOPE_BASE`/`ONELEVEL`/`SUBTREE`, `LDAP_AUTH_SIMPLE`/`NTLM`/
  `SSPI`/`DIGEST` and the rest, `LDAP_MOD_ADD`/`DELETE`/`REPLACE`, and field names
  such as `DistinguishedName`, `Message` and `ResultMessage`.

Main list columns (from a reference capture of the original, see
`analysis/AdInsight-original.png`):
`ID | Process | Request | Domain Controller | User | Input | Output | Result | Duration`,
over a detail pane with columns `Parameters | In/Out | Value`.

## What the recreation reproduces (core)

GUI exe `AdInsight.exe` (WTL, x64):
- Embedded manifest matched exactly to the original with `mt.exe`: identity
  "Insight for Active Directory" 1.2.0.0, **asInvoker** (not requireAdministrator),
  comctl32 v6 dependency, the same description and `dpiAware`.
- EULA gate on `HKCU\Software\Sysinternals\ADInsight\EulaAccepted` with
  `-accepteula` and `/accepteula`, plus the suite-level fallback.
- Single-instance guard counting `AdInsight.exe`.
- `WS_OVERLAPPEDWINDOW` main window, light mode via `PinToLightMode` and the
  three-edit toolbar-light fix (`PinToolbarLight`, `WM_ERASEBKGND`, `FillFrameBtnFace`).
- Toolbar, status bar, the main report list with the original's column set, a
  detail pane, a draggable splitter, autoscroll, find, clear, copy, save, font and
  always-on-top, and the About box.
- Capture Start drops and loads the embedded hook DLL, opens the
  `\\.\pipe\ADInsightPipe` server and arms the DLL through `AdmonTraceInit` and
  `AdmonMonitorStart`, exactly as the original does. Capture is on by default
  unless `-o`.
- A targeted `File -> Inject into Process...` menu item, and a headless
  `-inject <pid>` path for validation, that places the hook DLL into one process
  by id with `CreateRemoteThread(LoadLibraryW)`.

Hook DLL `ADInsightDll.dll` (x64):
- Exports `AdmonTraceInit`, `AdmonMonitorStart`, `AdmonMonitorStop` and
  `AdmonPingDLL`, matching the original's resolved names.
- When mapped into a process it patches that process's import address table for a
  core set of `wldap32` functions and reports each call over the named pipe. The
  hooked set is `ldap_initW`, `ldap_connect`, `ldap_simple_bind_sW`,
  `ldap_bind_sW`, `ldap_search_sW`, `ldap_search_ext_sW`, `ldap_result`,
  `ldap_get_valuesW` and `ldap_unbind`.
- Each call is logged on entry (request phase) and on return (response phase) with
  the LDAP result code and a duration measured with `QueryPerformanceCounter`, so
  the GUI pairs them by sequence number.
- The embedded DLL is the Release build, carried as the GUI's `BINRES` resource
  id `1170` under `res/AdInsightDll.dll`.

## Security

- The bind password is never captured, logged or forwarded. The
  `ldap_simple_bind_sW` and `ldap_bind_sW` hooks record only the bind DN and the
  auth method, not the credential.
- Validation used only a benign test client making synthetic calls against the
  dummy target `localhost:389` (nothing listening) with placeholder values. No
  real directory, no real credentials, no production identity server.
- The hook DLL was placed only into the test process spawned for validation, by
  its PID. It was never injected into any system, third-party or production
  process, and no kernel driver is involved. The all-process global-hook path is
  stubbed (see out of scope).

## Validation (synthetic only)

Tiny test client `test/ldaptest.cpp` makes a loop of `ldap_initW`,
`ldap_connect`, `ldap_simple_bind_sW` (placeholder DN and placeholder password),
`ldap_search_sW` and `ldap_unbind` against `localhost:389`. Harness
`validate.ps1`:
1. Launched `AdInsight.exe -accepteula` (capture auto-on, pipe server up).
2. Spawned `ldaptest.exe`, read its PID.
3. Ran `AdInsight.exe -inject <pid>` to place the hook DLL into the test process.
4. Confirmed the GUI captured the calls.

Results (see `analysis/AdInsight-rebuild.png`):
- Captured rows read through the listview: **9** calls, each from `ldaptest.exe`
  with the correct `Request` (`ldap_search_sW`, `ldap_initW`, `ldap_connect`,
  `ldap_simple_bind_sW`, `ldap_unbind`), the `Domain Controller` host for the
  connect calls, the bind DN under `User`, the parameters under `Input`, and the
  `Result` and `Duration`. The bind row shows the DN and the method only, no
  password.
- Toolbar `ToolbarWindow32` empty area sampled at RGB **240,240,240** (light,
  matching the original).
- Main window geometry `1920 x 1023`, the same default size the original opens at
  on this display (`analysis/AdInsight-original.png`).
- About box renders the exact credit line and `Author: 0xntpower`
  (`analysis/AdInsight-about.png`).
- Column layout matches the original one for one.

### Re-validated after the MainFrame.cpp split (2026-11-18)

The refactor that moved the `CMainFrame` method bodies out of `MainFrame.h`
(1001 lines) into a new `MainFrame.cpp`, leaving the header at declarations
only, is behaviour-preserving. Both x64 configurations (Debug and Release)
rebuild clean through `AdInsight.vcxproj`, which builds and embeds the hook DLL
first. The synthetic harness was re-run against the rebuilt Release GUI:
- Launched `AdInsight.exe -accepteula`, spawned `ldaptest.exe`, then ran
  `AdInsight.exe -inject <pid>` on that benign client.
- The GUI captured **8** rows, every one from `ldaptest.exe`, with the correct
  `Request` (`ldap_initW`, `ldap_connect`, `ldap_simple_bind_sW`,
  `ldap_search_sW`, `ldap_unbind`), `localhost` as the `Domain Controller` on
  the connect call, the bind DN under `User`, and the parameters under `Input`.
- The bind row records only the DN and `Method: LDAP_AUTH_SIMPLE` with
  `Credentials: (present)`. A read of every captured cell confirms the test
  client's placeholder password never appears.
- Toolbar `ToolbarWindow32` empty area (child of the frame, found with
  `FindWindowExW`) sampled at RGB **240,240,240**.
- Main window geometry **1920 x 1023**, the same default size documented above.

## Build

VS 2022 Community MSBuild, PlatformToolset v143, `stdcpp20`, Unicode, static CRT.
All configurations build clean:
- `AdInsightDll.vcxproj` (DynamicLibrary) Debug and Release.
- `AdInsight.vcxproj` (Application) Debug and Release.
- `test/ldaptest.vcxproj` (console validation client) Debug and Release.

Build order matters, because the GUI embeds the DLL: build the DLL, copy
`AdInsightDll/x64/Release/ADInsightDll.dll` to `res/AdInsightDll.dll`, then build
the GUI. The projects are not added to any solution.

## Out of scope (stubbed and documented)

- **System-wide auto-injection** into all running and newly started processes. The
  original installs a global `SetWindowsHookEx`. The recreation keeps `AdmonMonitorStart`
  and `AdmonMonitorStop` as the capture toggle and uses targeted `CreateRemoteThread`
  injection into one benign process for validation. The all-process path is not
  armed, for safety.
- **Injection into protected or system processes.** Not attempted.
- **Exhaustive per-operation attribute decode.** The recreation decodes a core set
  (scope, auth method, base, filter, DN, host, port). The original's full decode and
  its `Parameters | In/Out | Value` detail list are not reproduced. The recreation
  uses a single detail pane instead.
- **Save and compare trace file format.** Save writes a readable tab-separated
  export rather than the original's binary trace format. Load and compare are not
  reproduced.
- Filtering (process and class), column selection, and the multi-window
  `New Window` are present as menu items and stubbed.
