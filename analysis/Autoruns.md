# Autoruns (Sysinternals) recreation

Source-accurate C++20 plus WTL recreation of a core subset of the Sysinternals
Autoruns autostart viewer, reverse-engineered from `targets/sysinternals/Autoruns64.exe`
(version 14.3, a recent build). The project lives at `src/Autoruns/`, built x64
Debug and Release with VS 2022 Community MSBuild, toolset v143.

Autoruns is large. The approach was to survey the binary, confirm the durable
facts (registry keys, value names, the service and driver filters, the EULA
flow, the manifest and the command identifiers), and then reproduce a focused
core subset well rather than every provider.

## Reverse-engineering findings

Engine: IDA idalib MCP over the existing `Autoruns64.exe.i64`.

- **Config key**: `HKCU\Software\Sysinternals\Autoruns` (string at `0x1400da1e8`).
  The EULA uses `Software\Sysinternals\%s` with the app name filled in.
- **EULA**: `sub_140062810` checks and writes a REG_DWORD named `EulaAccepted`
  under the per-app key, after a dialog or a console Y and N prompt on Nano and
  IoT. `-accepteula` and `/accepteula` skip the dialog and record consent. The
  recreation mirrors this in `Eula.h`.
- **Filter value names** (in `sub_14002F640`, `sub_140030F40` and others):
  `HideEmptyEntries`, `HideMicrosoftEntries`, `HideWindowsEntries` and
  `HideVirusTotalCleanEntries`. The recreation persists the three in-scope ones
  under the config key with those exact names.
- **Services and Drivers** (`sub_1400891E0`): the provider reads each
  `HKLM\System\CurrentControlSet\Services\<name>` subkey, keeps a service when
  `Type` carries a Win32 service bit and `Start` is auto or demand, and a driver
  when `Type` is a kernel or file-system driver. The image comes from
  `ServiceDll` (in the key or the `Parameters` subkey) for a shared host,
  otherwise from `ImagePath`. The enabled state follows `Start` not equal to 4
  (disabled). The recreation reproduces this in `Enumerators.cpp`.
- **Manifest**: the embedded manifest is `asInvoker`, with two
  `Microsoft.Windows.Common-Controls` version 6 dependencies, `dpiAware` true,
  `dpiAwareness` `PerMonitorV2, system`, `gdiScaling` true and four `supportedOS`
  ids. Autoruns elevates in-app through a Run as Administrator path rather than
  requiring admin up front. The recreation's `Autoruns.manifest` matches this
  byte-for-byte in structure (verified with `mt.exe` on both binaries).
- **Command identifiers**: the menu uses the standard MFC values (`ID_FILE_OPEN`
  57601, `ID_FILE_SAVE` 57603, `ID_EDIT_COPY` 57634, `ID_EDIT_FIND` 57636,
  `ID_EDIT_CLEAR` 57632 for Delete, `ID_VIEW_REFRESH` 59395, `ID_APP_ABOUT`
  57664, `ID_APP_EXIT` 57665) plus Autoruns items in the 0x8000 range
  (Jump to Entry 32804, Jump to Image 32805, Verify Image 32806, the three Hide
  filters 32811 to 32813, and the rest). The recreated menu reuses the extracted
  numeric ids.
- **Publisher format**: the binary shows `(Verified) <signer>` and
  `(Not Verified) <company>` (both with a capital V, confirmed by the live
  original showing `(Not Verified) TMOG`). The service-host special case string
  `(SvcHost not Verified)` is at `0x1400e8798`.

## Core subset reproduced

Tabs: an **Everything** aggregate plus **Logon**, **Services**, **Drivers**,
**Scheduled Tasks** and **Winlogon**. Columns: **Autorun Entry**, **Description**,
**Publisher**, **Image Path**, **Timestamp**, a stubbed **Virus Total** column and
the enabled checkbox. Entries group under bold location header rows, as in the
original.

- **Logon**: HKLM and HKCU `...\CurrentVersion\Run` and `RunOnce` (plus the HKLM
  Wow6432Node Run), and the common and per-user Startup folders. Startup-folder
  shortcuts resolve to their target through `IShellLink`.
- **Services**: `HKLM\System\CurrentControlSet\Services` filtered to Win32
  services with auto or demand start.
- **Drivers**: the same hive filtered to kernel and file-system drivers.
- **Scheduled Tasks**: the Task Scheduler 2.0 COM store, walked recursively,
  with the exec action path as the image.
- **Winlogon**: `Userinit`, `Shell`, `Taskman`, `VmApplet`, `GinaDLL`, `System`
  and `AppSetup`.

Description and company come from the image version info (`GetFileVersionInfo`).
Signature verification runs `WinVerifyTrust` with
`WINTRUST_ACTION_GENERIC_VERIFY_V2` on a background thread. It verifies the
embedded signature first, then falls back to catalog verification for the many
Windows components that carry no embedded signature. The catalog path hashes the
file with SHA-256 (`CryptCATAdminAcquireContext2` plus
`CryptCATAdminCalcHashFromFileHandle2`), finds the catalog through
`CryptCATAdminEnumCatalogFromHash`, and verifies through `WTD_CHOICE_CATALOG`
with `WINTRUST_CATALOG_INFO.hCatAdmin` set so the member is hashed with the same
algorithm. The signer name comes from the catalog certificate, which reports
`Microsoft Windows` for OS files. Without the `hCatAdmin` field the verify
returns `TRUST_E_NOSIGNATURE` and every Windows image reads as unverified, which
was the one real bug found and fixed during validation.

The filters **Hide Windows entries**, **Hide Microsoft entries** and **Hide
empty locations** key off the signature result. A Microsoft entry is one
verified to a Microsoft signer, a Windows entry is a Microsoft one whose image
lives under the Windows directory, so nothing is hidden until verification
completes. Hide empty locations drops a location header with no visible entries.

Toolbar, menu, listview, Jump to Entry (regedit at the key through the
`LastKey` preference, or Explorer for a folder location), Jump to Image
(`explorer /select,<path>`), Verify Image, Properties, Copy and Find are all
present. The toolbar carries the light fix from the guide (`PinToolbarLight`
after it is built, the `WM_ERASEBKGND` handler returning `FillFrameBtnFace`).

`-accepteula` is honoured and acceptance is recorded as described above. The
About dialog shows the required credit and `Author: 0xntpower`.

## Read-only and safety

This recreation only enumerates and displays. It never deletes, disables,
unchecks or edits a real autorun entry. The Delete menu item is wired to an
explicit read-only notice. The one registry write outside its own config is
regedit's `LastKey` preference for Jump to Entry, which touches no autorun entry.

## Out of scope

Reproduced as a tab or a menu item for fidelity, with an empty or stubbed body
and a notice where a command is invoked:

- VirusTotal integration and its column (the column is shown, always empty).
- The other provider tabs, all shown empty: Explorer, Internet Explorer, Codecs,
  Boot Execute, Image Hijacks, AppInit, KnownDLLs, Winsock Providers, Print
  Monitors, LSA Providers, Network Providers, WMI, Office.
- Compare, Save and Open of the `.arn` scan file.
- Analyze Offline System.
- Search Online and Process Explorer launch.
- Scan Options dialog, Font, theme switching and Always On Top beyond the menu
  state.

Userinit and Shell are placed under the Winlogon category, matching where the
real Autoruns shows them, rather than duplicated under Logon.

## Validation

Both the original `Autoruns64.exe` and the rebuild were launched read-only on
the same machine and compared. Evidence captured during the session:

- **Manifest**: `mt.exe` on both binaries shows an identical structure,
  `asInvoker` with the two Common-Controls version 6 dependencies, the same dpi
  settings and the same four `supportedOS` ids.
- **Title and geometry**: the rebuild opens at
  `Autoruns - Sysinternals: www.sysinternals.com`, a wide default window with
  saved placement restored from the config.
- **Toolbar**: the `ToolbarWindow32` empty band reads 240,240,240 on a dark-mode
  system, so the light fix holds.
- **Entries**: the core locations match. Logon shows the same Run-key entries
  with matching image paths and publishers (SecurityHealth to
  `SecurityHealthSystray.exe` verified Microsoft Windows, third-party apps
  verified to Discord, Valve, Broadcom, Oracle, Blizzard, Google, Docker,
  Tailscale and others). Services found 68 Win32 services with auto or demand
  start, Drivers found 52 kernel and file-system drivers with the disabled ones
  shown unchecked, Scheduled Tasks found 156 tasks with their exec image paths,
  and Winlogon resolved Userinit, Shell and VmApplet to verified Microsoft
  Windows images.
- **Publisher and signature**: verified images show `(Verified) <signer>` and
  unverified images show `(Not Verified) <company>`, matching the original's
  formatting, including catalog-signed Windows files reported as
  `(Verified) Microsoft Windows`.
- **About**: the credit line renders as required.

## Build

- `MSBuild src\Autoruns\Autoruns.vcxproj /p:Configuration=Debug /p:Platform=x64`
- `MSBuild src\Autoruns\Autoruns.vcxproj /p:Configuration=Release /p:Platform=x64`

Both configurations build clean. The only warning is the shared `/Ob3` override
from `Directory.Build.targets`, common to the other tools.
