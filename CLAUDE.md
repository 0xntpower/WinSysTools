# WinSysTools (rews) — operating guide

Source-accurate C++20 plus WTL recreations of 18 NirSoft Windows utilities, one Visual Studio project each under `src/`, reverse-engineered from the original binaries in `targets/`. Private repo `github.com/0xntpower/WinSysTools`, branch `main`.

Read this first, then the two living documents it points to. It captures the workflow and the hard-won lessons so a fresh session repeats what works instead of relearning it.

## Sources of truth
- `analysis/RECONSTRUCTION-GUIDE.md` — the full per-tool pipeline, conventions, and gotchas. Read it before building or changing any tool.
- `analysis/<Tool>.md` — per-tool reverse-engineering notes and validation evidence.
- The auto-memory `project-rews-nirsoft-recreations.md` — live status and next actions.

## Workflow that works
- Reverse engineer with the **IDA idalib MCP** as the primary decompiler and **rea** (Ghidra backend) as a cross-check. IDA decompiles, rea verifies.
- Drive one tool end to end per worker: RE, then implement in C++20 plus WTL, then build, then validate. Scale with parallel subagents, each tool independent, each briefed to read the guide.
- Build with **VS 2022 Community MSBuild**, PlatformToolset **v143**. ATL ships only under VS 2022 Community's 14.44, not the VS 2026 one, so that MSBuild must be used. WTL headers live at `C:\WTL10_10320_Release\Include`. Build from PowerShell.
- Release hardening and speed flags are centralized in `Directory.Build.targets`. Do not set them per project.

## Hard-won wisdom (do not relearn these)
- **Dark toolbar.** The comctl32 v6 tools draw a dark themed toolbar band on a dark-mode system, and `PinToLightMode` does NOT reach the toolbar. Each v6 frame needs the three-edit fix from the guide (`PinToolbarLight` after the toolbar is made, the `WM_ERASEBKGND` handler, the message-map entry). MIMEView is the one exception, classic controls with no v6 manifest.
- **Stale binaries mislead.** Most tools carry both Win32 and x64, each with Debug and Release. After any change to a shared header rebuild **every** config, or you verify a stale build and draw the wrong conclusion. A single second-hand screenshot is not proof. Capture the live result and check it with real pixels.
- **Window geometry.** Pass the original's `CreateWindowEx` rect to `CreateEx`. A tool that starts with `CW_USEDEFAULT` keeps the WTL default.
- **Admin tools.** The requireAdministrator tools launch silently and with no UAC prompt through `.tools/launch-elevated.ps1`. The dark-toolbar fix holds elevated too.
- **Validation is first class.** A tool is done when its `/s` export is byte-identical to the original on the same input, its window geometry matches with the config cleared, its format or crypto round-trips match the real binary, and its toolbar child window reads 240,240,240.

## Guardrails
- **No self-credit or co-author lines in commits.** Commit messages and docs use a plain, direct voice. A `humanize` hook rejects any Markdown, C++ comment, or commit message that contains a semicolon character or a few flagged words. Avoid them in those places. Normal code may use semicolons.
- **Do not commit the originals** in `targets/` (freeware redistribution), and the `.tools/` launcher scripts stay gitignored.
- **Sensitive data.** ProductKeyScanner surfaces real product keys, CredentialsFileView real credentials, the sniffers real passwords, OutlookAttachView real mail. Never open, capture, log, screenshot, or transmit real secrets or personal data. Validate parsers against synthetic or known vectors, and crop any screenshot to the top strip.
- **Match the original exactly** on window geometry, toolbar look, light mode, manifest, and data format. Where the original has a defect such as a fixed-buffer truncation, the recreation may keep full fidelity instead and note it.

## Code quality — HRC v1.1, applied gently
The house bar is **HRC v1.1**, the High-Reliability C++ standard at `C:\Users\itag7\ll_projects\DriverForge\docs\HRC_Standard_v1.1\HRC_STANDARD.md`. Treat it as the aspiration for new and changed code, applied through its own Engineering Discipline section (section 18): minimum viable change, surgical edits, match the existing patterns. The current rews code already lines up with much of it — Allman braces, RAII throughout, smart pointers, PascalCase functions.

On code you write or touch, honour the cheap high-value rules: RAII for every resource, smart pointers with no raw owning `new` or `delete`, `nullptr` over NULL, initialize every variable, `static_cast` over C-style casts, `[[nodiscard]]` on results and status returns, a check on every OS and API call, const-correctness, `enum class`, named `constexpr` for magic numbers, and no unbounded C string functions. The standard numbers each of these if you want the detail.

Keep it a guiding line, not a gate. Do NOT retroactively reformat the 15 finished tools to the HRC naming rules (member trailing underscore, kPascalCase constants) — that churn fights the surgical-change rule and is out of scope. Adopt the naming and the mechanical backing (the HRC `.clang-format` Allman config and the `.clang-tidy` baseline) only when a tool is rebuilt anyway, or as a separate pass the user asks for. The kernel-mode section does not apply here — these are user-mode GUI tools.

## Quick reference
- MSBuild: `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`
- Build one config: `MSBuild src\<Tool>\<Tool>.vcxproj /p:Configuration=Release /p:Platform=x64 /m`
- Elevated launch: `powershell -File .tools\launch-elevated.ps1 -Exe <full path> -ToolArgs @('/scomma','out.csv')`
- rea CLI: `C:/Users/itag7/AppData/Roaming/npm/rea.cmd`
