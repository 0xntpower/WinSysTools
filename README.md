# WinSysTools

Independent, source-accurate recreations of a set of NirSoft Windows system utilities, reconstructed from binary analysis and rewritten as modern **C++20 with WTL**. Each tool is reverse-engineered from its original binary (IDA Pro, with rea/Ghidra as an evidence cross-check), then reimplemented from scratch with real data structures and idiomatic code rather than a transliteration of the decompiled output. The result stays faithful to the original in behaviour and appearance. The original UI resources (menus, dialogs, icons, accelerators) are extracted and reused so the windows look identical.

Everything lives in one Visual Studio solution, `rews.sln`, with one project per tool under `src/`. All 18 tools are complete.

## Toolchain
- Visual Studio 2022 (v143), C++20, built for x86 or x64 to match each original.
- WTL 10 (headers), ATL (ships with the v143 toolset).
- Reverse engineering with IDA Pro (idalib) as the primary decompiler and rea (Ghidra backend) as the evidence and cross-check engine.

See `CLAUDE.md` for the full workflow, the hard-won lessons, and the code-quality bar (HRC v1.1, applied gently). The repeatable per-tool pipeline, conventions, and gotchas live in `analysis/RECONSTRUCTION-GUIDE.md`.

## Build
Open `rews.sln` in Visual Studio 2022, or from a developer prompt:

```
msbuild src\MIMEView\MIMEView.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The x64 tools build with `/p:Platform=x64`. Release binaries carry the hardening and speed flags from `Directory.Build.targets` (Control Flow Guard, CET, high-entropy ASLR, LTCG, static CRT).

## Tools

| Tool | Arch | What it does |
|------|------|--------------|
| MIMEView | x86 | Internet Explorer MIME types from the registry |
| VNCPassView | x86 | Recover VNC passwords (fixed-key DES) |
| RemoteDesktopPassView (rdpv) | x86 | Recover `.rdp` passwords (DPAPI) |
| IconsExtract (iconsext) | x86 | Extract icons and cursors from PE files |
| OfficeIns | x64 | Microsoft Office add-ins manager |
| ProductKeyScanner | x64 | Scan and decode product keys |
| WinUpdatesView | x64 | Windows Update history |
| HeapMemView | x64 | Heap memory blocks of a running process |
| SimpleWMIView | x64 | WMI query browser |
| TaskSchedulerView | x64 | Scheduled tasks from the Task Scheduler |
| FileTypesMan | x64, admin | File type and extension associations manager |
| CredentialsFileView | x64 | Decrypt Windows Credential Manager files (DPAPI) |
| SimpleProgramDebugger | x64 | Debug-event logger (process, thread, module, exception) |
| ShadowCopyView | x64, admin | Browse files inside Volume Shadow Copies (VSS) |
| PreviousFilesRecovery | x64, admin | Recover previous file versions from shadow copies |
| SmartSniff (smsniff) | x64, admin | Packet sniffer (Npcap, WinPcap, or NetMon) |
| SniffPass | x64, admin | Password sniffer for cleartext auth protocols |
| OutlookAttachView | x64 | List Outlook message attachments through Extended MAPI |

Per-tool reverse-engineering notes are in `analysis/<Tool>.md`.

## Validation
Each tool is checked against its original where possible: a byte-for-byte diff of the command-line save output, crypto or format round-trips against the real binary, a window-geometry match with the config cleared, and a live screenshot. Highlights:

- Many tools reproduce the original's `/s*` export byte-for-byte on the same input, among them MIMEView, OfficeIns, FileTypesMan, TaskSchedulerView, SimpleWMIView, WinUpdatesView, ShadowCopyView, PreviousFilesRecovery, and SimpleProgramDebugger.
- IconsExtract produces byte-identical `.ico` files from `shell32.dll`.
- The VNC, RDP, ProductKeyScanner, and CredentialsFileView decryptors recover the same values as the originals, validated with known vectors rather than real secrets.
- The SmartSniff and SniffPass packet and credential parsers pass self-checks against synthetic captures, never real traffic.
- Every window matches the original's launch size and show-state, and the toolbar renders light to match the originals on a dark-mode system.

The sniffers load their capture driver (Npcap or WinPcap) dynamically, and OutlookAttachView loads Extended MAPI dynamically, so each builds and runs without the dependency present and reports it missing rather than failing.

## Appearance and light mode
The original tools do not follow the Windows dark theme. The recreations pin the common controls to light mode and de-theme the toolbar, so the icon bar stays light on a dark-mode system and matches the originals. `CLAUDE.md` and the reconstruction guide record the exact handling.

## Notes on the original tools
These are clean recreations for study and interoperability. The original NirSoft binaries are **not** included in this repository. UI resources were extracted from the originals to preserve exact visual appearance. All original tools are the work of Nir Sofer (nirsoft.net). No license is set on this repository yet. Add one before any wider distribution, and revisit the reused UI resources if the repository is ever made public.
