# WinSysTools

Independent, source-accurate recreations of NirSoft and Sysinternals Windows utilities by **0xntpower**. Each tool is reverse-engineered from its original binary (IDA Pro primary, rea/Ghidra cross-check) and rewritten from scratch in modern **C++20** — the GUI tools with **WTL 10**, the Sysinternals command-line tools as plain console apps. The aim is faithful behaviour rather than a transliteration of decompiled output, and the GUI tools reuse the originals' menu, dialog, and icon resources so the windows look identical.

Two Visual Studio solutions, one project per tool under `src/`:
- **`rews.sln`** — the 18 NirSoft GUI tools (WTL).
- **`sysinternals.sln`** — the Sysinternals tools (console, plus the GUI ones).

## Status
- **18 NirSoft tools** — complete and validated.
- **20 Sysinternals command-line tools** — complete and validated (Strings, Coreinfo, NTFSInfo, Whois, LoadOrder, AdRestore, ProcDump, PsExec, PsPing, and the full PsTools suite).
- **Sysinternals GUI tools** (WinObj, DebugView, VMMap, RamMap, and the rest) — in progress.

Per-tool reverse-engineering notes and validation evidence live in `analysis/<Tool>.md`. The workflow, conventions, and hard-won lessons are in `CLAUDE.md` and `analysis/RECONSTRUCTION-GUIDE.md`.

## Toolchain
- Visual Studio 2022 (PlatformToolset v143), C++20, built x86 or x64 to match each original.
- WTL 10 and ATL for the GUI tools.
- IDA Pro (idalib) as the primary decompiler, rea (Ghidra backend) as the cross-check.

## Build
Open a solution in Visual Studio 2022, or from a developer prompt:

```
msbuild sysinternals.sln /p:Configuration=Release /p:Platform=x64
```

Release binaries carry the hardening and speed settings from `Directory.Build.targets` (Control Flow Guard, CET, high-entropy ASLR, LTCG, static CRT).

## Validation
Each tool is checked against its original where it can be. The command-line tools diff their stdout byte-for-byte, the GUI tools diff their `/s*` export and match window geometry, and the crypto and format paths round-trip against the real binary. Tools that surface sensitive data (product keys, credentials, passwords, mailboxes) are validated against synthetic or known vectors, never real data. The sniffers and OutlookAttachView load their capture driver or MAPI dynamically, so they build and run without the dependency present.

## Appearance
Each GUI tool pins its common controls to light mode and de-themes the toolbar, so the window matches the original on a dark-mode system. Every tool carries a one-line recreation credit, the About dialog on the GUI tools and the startup banner on the command-line tools, naming 0xntpower and the original suite.

## Notes
These are clean recreations for study and interoperability. The original NirSoft and Sysinternals binaries are **not** included in this repository, and their UI resources were extracted only to preserve the exact look. No license is set yet, so add one before any wider distribution.
