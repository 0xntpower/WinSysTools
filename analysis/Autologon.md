# Autologon

Source-accurate x64 recreation of the Sysinternals **Autologon** utility, a small
WTL dialog that configures Windows automatic logon. Original binary
`targets/sysinternals/Autologon64.exe` (v3.1, MD5 ffca2fbcc7e4637f2acc0a8a3b0dbaaa).
Engines: IDA idalib MCP primary, rea cross-check.

## What it does

A single modal dialog with Username, Domain and Password fields and three buttons,
Enable, Disable and About. Enable writes the Winlogon autologon values and stores
the password as an LSA private secret. Disable clears them. The same enable path is
reachable from the command line as `autologon <username> <domain> <password>`.

## Binary overview

x64, 5 sections, 2724 functions. No packer. Imports that matter: `LsaOpenPolicy`,
`LsaStorePrivateData`, `LsaClose`, `LogonUserW`, the `Reg*` family,
`OpenProcessToken` plus `GetTokenInformation` plus `LookupAccountSidW`,
`CommandLineToArgvW`, `CreateDialogParamW` and `DialogBoxParamW`. The resource
section holds two named dialog templates (ABOUT and AUTOLOG), a VERSION block and a
requireAdministrator MANIFEST. It carries **no icon resource**, so the original's
`LoadIconW(hInstance, "APPICON")` returns null and the window shows the default
application icon. The recreation matches that and embeds no icon.

## Reverse engineering findings

### WinMain (0x140003570)

1. `CommandLineToArgvW`, then `sub_140002300` handles the EULA. That helper strips
   `/accepteula` and `-accepteula` from argv (decrementing argc) and runs the EULA
   gate `sub_140001DB0`. When the gate returns not accepted, WinMain exits.
2. A Windows version guard (XP or later) through `VerifyVersionInfoW`.
3. `IsWow64Process` sets `dword_140065DA8` to `KEY_WOW64_64KEY` only when the
   process runs under WOW64. A native x64 process is never WOW64, so at run time the
   flag is 0 and it is OR'ed into every Winlogon `samDesired`.
4. Dispatch on argc after the accepteula strip:
   - argc 1 or less: register a window class named AUTOLOG and create the dialog
     from template AUTOLOG with proc `sub_140003020`, then run a message loop.
   - argc 4 (three positional arguments): silent enable through
     `sub_140002910(nullptr, 1, user, domain, password)`, no message boxes.
   - otherwise: `Usage: autologon <username> <domain> <password>`.

### Main dialog proc sub_140003020

A hybrid window proc used as both the AUTOLOG window class proc and the dialog proc.

- WM_INITDIALOG: centre the window on the primary display with the horizontal
  position nudged by 4 pixels and aligned down to an 8 pixel boundary. Read the
  current account through `OpenProcessToken` plus `GetTokenInformation(TokenUser)`
  plus `LookupAccountSidW` as the default, then open Winlogon read only and let an
  existing DefaultUserName, DefaultDomainName and DefaultPassword override the
  defaults. The three values fill control IDs 1000, 1001 and 1002.
- WM_COMMAND id 1 (Enable, IDOK): read the three fields. An empty password shows
  `AutoLogon does not support NULL passwords.` Otherwise call `sub_140002910` in
  interactive mode. On success the dialog closes.
- WM_COMMAND id 2 (Disable, IDCANCEL): open Winlogon, delete DefaultUserName,
  DefaultPassword and DefaultDomainName, set AutoAdminLogon to `0`, show
  `AutoLogon is disabled.` and close. It does not clear the LSA secret.
- WM_COMMAND id 3 (About): `DialogBoxParamW("About", sub_140002680)`.
- WM_DESTROY: end the dialog and post quit.

### Enable logic sub_140002910

1. `LogonUserW(user, domain, password, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT)`
   validates the credentials. Failure shows `The user name or password is invalid`.
2. Open `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon` with
   `wow64flag | KEY_WRITE`.
3. `RegSetValueExW` DefaultUserName and DefaultDomainName as REG_SZ.
4. `LsaOpenPolicy(POLICY_CREATE_SECRET)`, build an LSA_UNICODE_STRING for the secret
   name `DefaultPassword` and one for the password value, then
   `LsaStorePrivateData(policy, "DefaultPassword", password)`. When LSA open or store
   fails, fall back to a REG_SZ DefaultPassword value and remember that the password
   is unencrypted. When the secret stores, delete any stale DefaultPassword value.
5. Open `HKLM\System\CurrentControlSet\Control` with full access and delete the `EAS`
   subkey, which is the device-lock policy that can block autologon.
6. `RegSetValueExW` AutoAdminLogon to `1`.
7. Success report: `encrypted` with an information icon when the LSA secret stored,
   `NOT encrypted` with a warning icon on the fallback.

Return codes 0 through 5 map to success, open-key failure, user-write failure,
domain-write failure, password-write failure and AutoAdminLogon failure.

### EULA gate sub_140001DB0, sub_140001460, sub_140001500

The per-tool flag is `HKCU\Software\Sysinternals\Autologon\EulaAccepted` as a
REG_DWORD. `-accepteula` or `/accepteula` writes it and proceeds. The original also
honours the machine-wide and user-wide `Software\Sysinternals` keys, and when none
is set it shows the full license in a RichEdit dialog built in code, or a console
prompt when stdin is a pipe.

### Registry and LSA summary

| Item | Value |
|------|-------|
| Winlogon key | `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon` |
| Values | `DefaultUserName`, `DefaultDomainName`, `DefaultPassword` (REG_SZ), `AutoAdminLogon` (`1` or `0`) |
| LSA secret | private data name `DefaultPassword` through `LsaStorePrivateData` |
| EAS cleanup | delete `EAS` under `HKLM\System\CurrentControlSet\Control` |
| EULA flag | `HKCU\Software\Sysinternals\Autologon\EulaAccepted` (REG_DWORD) |

### Dialog templates

AUTOLOG is 202 by 78 dialog units, caption `Autologon - Sysinternals`, FONT 8 MS
Shell Dlg. Controls: edits 1000, 1001 and 1002 at 52,9 and 52,25 and 52,41 sized
85 by 12, with 1002 ES_PASSWORD. Button Enable (IDOK) at 144,9, Disable (IDCANCEL)
at 144,26 and About (id 3) at 144,58, each 50 by 14. Three left labels at x 7. The
original template names a custom window class Autolog.

## Recreation

| File | Role |
|------|------|
| `Autologon.cpp` | WinMain, argv, the EULA gate, mode dispatch |
| `AutoLogon.h` | enable, disable, read-state and LSA secret logic, plus the EULA flag helpers |
| `MainDlg.h` | CMainDialog (AUTOLOG) and CAboutDialog |
| `res/Autologon.rc` | both dialog templates, version block, manifest reference |
| `res/Autologon.manifest` | requireAdministrator, Common-Controls v6 |
| `resource.h` | numeric IDs |

`PinToLightMode()` runs before `InitCommonControlsEx`. The dialog has no toolbar, so
the dark-toolbar fix does not apply. The RAII `ScopedHKey`, `[[nodiscard]]` results,
`nullptr`, `static_cast`, named constants and a check on every API call follow HRC
v1.1 as a guiding line.

### Deliberate deviations

- The custom window class Autolog is dropped so a standard WTL CDialogImpl binds the
  template. The client-area layout stays identical. The outer window frame differs by
  about 5 pixels per side because the standard dialog class carries a slightly
  different non-client frame.
- The dialog templates get numeric IDs rather than the original named resources, so
  CDialogImpl binds them directly.
- The EULA is presented as an acceptance message box with the `-accepteula` hint
  rather than the original RichEdit license viewer. The flag path and the switch are
  faithful.
- The About credit replaces the Russinovich copyright, as required for this project.
- No icon resource, matching the original.
- The Windows version guard and the IoT and Nano Server guards are out of scope and
  omitted.

## Validation

Both binaries were launched elevated through `.tools/launch-elevated.ps1` with
`-accepteula`, captured, and closed without ever clicking Enable or Disable.

### Layout and fields, side by side

The enumerated child controls match in id, class, size, spacing and caption. The
original screenshot and the rebuild screenshot show the same Username, Domain and
Password rows with the same Enable, Disable and About buttons. Both pre-filled
Username `itag7` and Domain `.` from the current Winlogon state, with an empty
Password because no DefaultPassword value is present.

Child control sizes are identical: edits 128 by 20, buttons 75 by 23, labels 53, 41
and 51 wide by 13 high. Row spacing is 26 pixels in both. The control block sits 5
pixels further in on each axis in the rebuild, which follows from the 5 pixel frame
difference noted above.

### Geometry

Original window 309 by 156, rebuild 319 by 166. The 10 pixel difference is the frame,
not the layout. Both come from the same 202 by 78 dialog-unit template at FONT 8 MS
Shell Dlg, so the client area and every control rectangle match.

### About credit

The rebuild About shows exactly:

```
Autologon v3.1
Independent source-accurate recreation of Autologon by Sysinternals, through reverse engineering and rewritten in modern C++20 & WTL 10.
Author: 0xntpower
```

The `&&` in the source renders as a single `&` in the static control.

### Enable and disable logic, by code inspection

Validated against the decompile, not by running them on this machine.

- `autologon::Enable` matches `sub_140002910`: LogonUserW validation, open Winlogon
  with the WOW64 flag OR KEY_WRITE, write DefaultUserName and DefaultDomainName, store
  the LSA secret with the plaintext fallback and stale-value delete, remove the EAS
  key, set AutoAdminLogon to `1`, and report encrypted versus unencrypted.
- `autologon::Disable` matches the WM_COMMAND id 2 path: delete the three values, set
  AutoAdminLogon to `0`, and leave the LSA secret in place as the original does.
- `autologon::ReadCurrentState` matches the WM_INITDIALOG read.

### Security

No autologon was enabled on this machine and no DefaultPassword secret was stored.
Before and after validation the Winlogon state was unchanged: AutoAdminLogon `0`,
DefaultUserName `itag7`, DefaultDomainName `.`, DefaultPassword absent. The only
change was the expected `HKCU\Software\Sysinternals\Autologon\EulaAccepted` flag set
by `-accepteula`. No credential was ever synthesized, and the Enable and Disable
buttons were never clicked during capture.

## Build

VS 2022 Community MSBuild, v143, stdcpp20, Unicode, static CRT, SubSystem Windows,
requireAdministrator manifest. Debug and Release x64 both build clean. The only note
is the shared `/Ob2` to `/Ob3` override from `Directory.Build.targets`, common to
every tool. Output `src/Autologon/x64/Release/Autologon.exe`.
