# PsPasswd

Source-accurate C++20 console reconstruction of Sysinternals `pspasswd64.exe`
v1.25 (Unicode, x64). PsPasswd changes the password of a local or domain account
on the local computer or on one or more remote computers.

Reconstruction lives in `src/PsPasswd/`. The shared PsTools front end in
`src/common/PsTools.h` is reused read-only for the banner, EULA gate, target
iteration, alternate credentials, system-error reporting, local identity, and the
hidden password prompt.

## Engines

- IDA idalib MCP (primary) on `targets/sysinternals/pspasswd64.exe`.
- rea / Ghidra (cross-check) on the core worker, snapshot at
  `analysis/snapshots/pspasswd.snap`. Both engines agree on the core logic.

## Function map

| Address | Role |
|---------|------|
| `sub_140004440` (`main`) | banner and EULA driver, OS gate, parse, prompt, iterate |
| `sub_140003420` | banner print plus EULA gate, strips `-nobanner` and `-accepteula` |
| `sub_140003360` | OS gate, VerifyVersionInfo for Windows XP (5.1) or higher |
| `sub_140003EB0` | command-line parse |
| `sub_140004200` | usage |
| `sub_140002FB0` | ForEachComputer (target iteration) |
| `sub_140003D90` | per-computer worker, connect then change then report |
| `sub_140003AB0` | change one password, NetUserGetInfo and NetUserSetInfo level 1 |
| `sub_140003680` | remote connect, WNetAddConnection2W to `\\computer\IPC$` |
| `sub_140002F30` | clear the console line |

## What it does

The core worker reads the account with `NetUserGetInfo` at level 1 (USER_INFO_1),
overwrites only the `usri1_password` field at offset 8, and writes the whole
record back with `NetUserSetInfo` at level 1. Reading and writing the full record
keeps every other account setting unchanged. Both IDA and rea show the same three
calls and the same single-field overwrite.

A domain account is one whose name carries a backslash, like `DOMAIN\user`. For
that form the worker splits the name, finds a domain controller with
`NetGetAnyDCName`, and targets the returned server rather than a named computer.
A local account targets `\\computer` built from the computer name.

An omitted new password applies an empty password. The original defaults the new
password pointer to an empty string, and so does the reconstruction. A blank new
password and an omitted one behave the same way.

The task brief named level 1003 for the setinfo call. The binary does not use
1003. It reads USER_INFO_1 and writes it back at level 1, confirmed by both
engines. The reconstruction follows the binary.

## Switches and grammar

```
pspasswd [\\computer[,...] | @file] [-u user [-p psswd]] Account [NewPassword]
pspasswd <Domain\Account> [NewPassword]
```

- `\\computer` targets one computer. A comma list, `@file`, or `\\*` (every
  server in the domain or workgroup) are handled by the shared iterator.
- `-u user` supplies alternate credentials and needs a computer target.
- `-p psswd` supplies the password for `-u`. Omitting it prompts for a hidden one.
- `-nobanner` and `-accepteula` are removed before parse by the banner driver.
- The first bare argument is the account. The argument right after it, if present,
  is the new password, taken verbatim whatever its leading character.

Validation that rejects to usage, matching the original:

1. An account is required.
2. `-u` requires a computer target.
3. `-p` requires `-u`.
4. A domain account cannot be combined with a computer target or with `-u`.

## Streams and exit codes

- Banner to stderr, or stdout when stdout is a pipe (shared PrintBanner).
- Usage to stdout.
- Progress (`Changing password for ...`) and the clear-line erase to stderr.
- Success (`Password successfully changed.`) to stdout.
- Error (`Error changing password:` plus the system message) to stderr.

Exit codes: EULA declined 1, OS gate failure -1, usage -1, change success 0,
change failure -1.

## Build

VS 2022 Community MSBuild, v143, stdcpp20, Unicode, static CRT, Console. Links
`advapi32`, `netapi32`, `mpr`. Both Debug and Release x64 build clean. The only
Release diagnostic is the harness-wide `/Ob2` to `/Ob3` override from
`Directory.Build.targets`, shared by every tool.

## Validation

Compared the reconstruction against the original with stdout, stderr, and exit
code captured on separate streams. All four safe paths match byte for byte.

| Case | Arguments | stdout | stderr | exit |
|------|-----------|--------|--------|------|
| usage | `-nobanner /?` | match | match | -1 both |
| bad switch | `-nobanner -z someacct` | match | match | -1 both |
| extra positional | `-nobanner acct newpass extra` | match | match | -1 both |
| nonexistent account | `-nobanner -accepteula rews_nonexistent_zzz987` | match | match | -1 both |

The nonexistent-account run exercises the change entry point. `NetUserGetInfo`
fails with "The user name could not be found." before any write, so no account is
touched. The progress line, the clear-line erase, the error header, and the
system message are identical to the original down to the byte (120 bytes on
stderr, empty stdout, exit -1).

### Paths exercised

- Banner, EULA gate, OS gate, parse, usage, and the per-computer report path were
  run against the live binaries and matched.
- The read branch of the change path (NetUserGetInfo) was run through the
  nonexistent-account case and matched.
- The write branch (NetUserSetInfo) was reproduced from the IDA decompile and
  cross-checked with rea. It was not run live.

### Why the live change was not run

A faithful change test needs a throwaway local account created and later deleted
under elevation. This session is not elevated, so creating that account with
`NetUserAdd` is not available, and the task forbids touching any real account.
Following the task fallback, only the safe parsing and error paths were run live,
and the write path was reproduced from code inspection. No account was created,
changed, or deleted.
