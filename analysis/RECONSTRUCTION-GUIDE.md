# Reconstruction guide (per-tool pipeline)

The canonical worked example is **`src/MIMEView/`** plus **`analysis/MIMEView.md`**. Read both before reconstructing another tool. Every tool follows the same loop.

## Goal per tool
Produce a clean, idiomatic **C++20 + WTL** recreation that is behaviourally and visually faithful to the original NirSoft binary, reusing the original's extracted resources, and that builds in the Visual Studio solution to the original's EXE name.

## Two framework generations
- **Old-gen x86** (iconsext, mimeview DONE, rdpv, vncpassview) — ANSI, VC6-era, small. MIMEView is the exemplar.
- **Modern x64** (the other 14) — Unicode, richer shared framework. The first x64 tool reconstructed should factor the shared pieces so the rest reuse them.

## Engines
- **IDA idalib MCP is the primary decompiler** (fast, typed Hex-Rays). Open with `idb_open(input_path, idle_ttl_sec=3600)`, then `survey_binary`, `decompile`, `xrefs_to`, `get_string`. The worker self-exits after idle_ttl, reopen as needed.
- **rea is the cross-check and evidence engine**, backed by Ghidra. `rea` lives at `C:/Users/itag7/AppData/Roaming/npm/rea.cmd`. `rea analyze <exe> --snapshot analysis/snapshots/<tool>.snap --format md` gives the authoritative overview (about 2 min). `rea decompile <exe> <addr> --snapshot ...` confirms a critical function (about 3 min each, use sparingly on the parsing, crypto, or registry core).

## Pipeline
1. **Triage** — `survey_binary` for arch, imports, strings, top functions. UPX-packed binaries (iconsext, rdpv) must be unpacked first with `upx -d` into a copy.
2. **Trace** — entry to WinMain to the window class and its WndProc (often a C++ vtable dispatch). Map WM_CREATE (controls plus columns), the data-load message, WM_COMMAND handlers, WM_DESTROY (save config).
3. **Data model** — find the enumerate or collect function and every registry key, file, API, and field it reads. Record the exact field-to-column mapping and column titles. Dump strings with `strings -t x` and anchor file offsets to an RVA via a known string.
4. **Resources** — extract with Resource Hacker, installed at `C:\Program Files (x86)\Resource Hacker\ResourceHacker.exe`. Run `ResourceHacker.exe -open <exe> -save src/<Proj>/res/<Proj>.rc -action extract -mask ,,`. This writes the .rc plus icon, bitmap, and cursor files.
5. **Write** clean C++20 plus WTL (see MIMEView files for the shape) — a small RAII `RegKey`, a named record struct, a focused enumerator, a `CFrameWindowImpl` main window with an owner-data `CListViewCtrl`, the toolbar and status bar from the original resources, WTL message and command maps, and the dialogs from the extracted templates.
6. **Build** — tailor the project's vcxproj (see `src/MIMEView/MIMEView.vcxproj`), then MSBuild Debug and Release.
7. **Validate** — run the original and the rebuild with the save-and-quit switch and diff the output. Capture both windows and compare. Record the result in `analysis/<Tool>.md`.

## Conventions
- WTL headers are at `C:\WTL10_10320_Release\Include` (add to IncludePath). ATL ships with the v143 toolset.
- C++20 (`stdcpp20`), Unicode for modern tools, MultiByte only if a tool's data is genuinely ANSI and matters. Reuse the MIMEView vcxproj as the template (WTL include path, libs comctl32, shell32, advapi32, comdlg32, ole32, defines `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `STRICT`, ConformanceMode off for the WTL translation unit).
- Keep logic clean and idiomatic rather than a transliteration of decompiled offsets. Preserve exact registry keys, value names, field semantics, column titles, config value names, command IDs, export formats, and command-line switches.
- Where the original has a fixed-buffer truncation or similar defect, the recreation may keep full fidelity of data instead (note it as a deliberate improvement).
- Toolbar: build it from the extracted strip with `#include "../common/UiUtil.h"` then `tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR))`. Do not use `tb.AddBitmap` / `TB_ADDBITMAP`, which leaves the strip's background (light gray on 4-bit strips, black on 24-bit strips) unmasked and renders as a dark box. The helper reads the bitmap's own corner colour as the transparent key.
- Window geometry: create the main window at the original's default position and size. Recover the `CreateWindowEx` X, Y, width and height from the original and pass that exact rect to `CreateEx` rather than the WTL default, so the first-launch window matches. Saved placement from the config still overrides on later launches. Verify by deleting the config, launching the original and the rebuild, and comparing `GetWindowRect` and `GetWindowPlacement`.
- Light mode and the toolbar: the original tools do not follow the system dark theme. Call `PinToLightMode()` (from `common/UiUtil.h`) at the top of WinMain, before `InitCommonControls`, which keeps the menus and list headers light. It does NOT reach the toolbar. The comctl32 v6 tools (almost all, through their manifest) draw a dark themed toolbar band that PinToLightMode cannot touch, so each frame needs three more edits. One, call `PinToolbarLight(tb)` right after the toolbar is created. Two, add `MSG_WM_ERASEBKGND(OnEraseBkgnd)` to the message map. Three, add a one-line `OnEraseBkgnd(CDCHandle dc)` handler that returns `FillFrameBtnFace(m_hWnd, dc)` (copy it from any built tool such as `src/VNCPassView/MainFrame.h`). That drops the toolbar visual style and fills the frame with the button-face colour, so the bar reads light. MIMEView skips this (classic controls, no v6 manifest, already light). Verify by capturing the `ToolbarWindow32` child window and checking its empty area reads 240,240,240, and rebuild every config (both platforms and both configurations) because a stale build hides the result.
- Launching admin-manifest tools: FileTypesMan, PreviousFilesRecovery, ShadowCopyView, SmartSniff (smsniff), and SniffPass carry `requireAdministrator` and would pop a UAC prompt on every launch. Launch them (original or rebuild) through `powershell -File C:\Users\itag7\ll_projects\rews\.tools\launch-elevated.ps1 -Exe <path> -ToolArgs @(...)`, which starts them elevated and silently through a pre-registered scheduled task and returns the launched PID. Use plain Start-Process for the other thirteen tools.

## Build settings
Release hardening and optimization live in `Directory.Build.targets` at the repo root and apply to every project automatically, so do not set per-project Release mitigation or optimization flags. Each Release binary gets Control Flow Guard, Spectre mitigation, high-entropy ASLR on x64, DEP, CET shadow stack, EH continuation metadata on x64, System32-only dependent loading, full optimization (`/O2 /Ob3 /GL /LTCG /OPT:REF,ICF`), and the static CRT (`/MT`) so the binary is standalone and launches without the CRT DLLs.

## Gotchas (all hit during MIMEView)
- `small` and `far` and `near` are reserved macros from the Windows headers. Never name a variable `small`.
- Define `NOMINMAX` or `std::min` and `std::max` break against the windows.h macros.
- Include `atlcrack.h` for the `MSG_WM_*` and `COMMAND_ID_HANDLER` cracker macros, and `atldlgs.h` for `CFileDialog` and `CFindReplaceDialog`.
- `CWindow` member calls such as `SetWindowTextW` and `SetDlgItemTextW` take no HWND argument, unlike the global APIs.
- WTL `atlres.h` predefines standard command IDs (ID_FILE_SAVE, ID_EDIT_COPY, ID_EDIT_FIND, ID_VIEW_REFRESH, and more). `#undef` any that collide before defining the tool's own values.
- Resource Hacker emits letter accelerators as `VK_A`, which rc.exe rejects. Replace with the quoted character, for example `"A"`.
- The extracted .rc uses symbolic styles without includes. Prepend `#include <windows.h>` and `#include <commctrl.h>` to it.
- A `humanize` hook rejects semicolons in Markdown and in C++ comments. Keep comments semicolon-free. Code statements are fine.

## Writing style for notes and commits
Write the per-tool notes and commit messages plainly and directly. Avoid em dashes in running sentences, filler words such as `leverage`, `robust`, `comprehensive`, and `seamless`, and marketing phrasing. State what the code does at the confidence the evidence supports. Do not add self-credit or co-author lines to commits.

## Status
Done and validated, with findings in `analysis/<Tool>.md`: MIMEView, VNCPassView, IconsExtract, RemoteDesktopPassView, OfficeIns, ProductKeyScanner, WinUpdatesView. Queued x64 self-contained tools: HeapMemView, ShadowCopyView, PreviousFilesRecovery, SimpleProgramDebugger, CredentialsFileView, SimpleWMIView, TaskSchedulerView, FileTypesMan. Queued dependency-heavy tools (need Npcap or Outlook): SmartSniff, SniffPass, OutlookAttachView.
