---
name: verify-build-launch-screenshot-archived
description: ARCHIVED (superseded by the "verify" skill, which runs scripts/verify.py). Kept for its input-injection-limitation writeup, which the new skill doesn't cover. Build, launch, and screenshot-verify ToonEngine.exe when a live interactive check is genuinely needed. Not a trigger for "verify" anymore.
---

# ARCHIVED: ToonEngine Build/Launch/Capture, and the Input-Injection Limitation

Superseded by the `verify` skill (`scripts/verify.py fast|full|deep`) for the "is it green"
use case. This doc survives because its input-injection findings (below) are still true and
not duplicated anywhere else: pull from here if a task specifically needs a live
screenshot/interaction check rather than the scripted tiers.

## Build + Launch (Works Reliably)

```
cmake --build --preset windows-debug
Start-Process build\windows-debug\ToonEngine.exe
```

Works from a bare shell as long as `build/windows-debug` is already configured *and*
`CMakeLists.txt` hasn't changed since: ninja caches tool paths from the last successful
configure, so the VS Developer environment doesn't need importing for a source-only
incremental build. A from-scratch *configure* does need it, and so does any build that
edits `CMakeLists.txt` (e.g. adding a new source file), which forces a reconfigure as the
build's first step, and DiligentTools' library-combining step invokes `lib.exe` by bare
name at build time, which fails with `'lib.exe' is not recognized` from a plain shell (see
repo MEMORY.md → "Build gotchas"). There's no repo script for importing the VS env
(deliberately; don't recreate one). Import it inline, chained into the *same* shell
invocation as the build itself (env vars set in one tool call don't persist to the next):

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 -no_logo && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "Env:$($matches[1])" $matches[2] }
}
cmake --build --preset windows-debug
```

**Cold start is slow on a debug build: wait ~10-12s before trusting any capture.**
First-run HLSL→SPIR-V cross-compilation + Vulkan PSO creation (debug validation layers on)
can block the app past a short retry budget; captures taken too early come back solid white
even though the app is running fine. Sleep ~10s after the window handle appears, *then*
start capturing/driving.

## Screenshotting (Works Reliably)

Vulkan swap-chain windows are GDI-black via normal screen copy. Use `PrintWindow` with
`PW_RENDERFULLCONTENT = 2`, and make the *capturing* process per-monitor DPI-aware
(`SetProcessDpiAwarenessContext(-4)`) or right-docked panels get cropped on a scaled
display. Retry on an all-white frame (present race / still-compiling). Sample a few
pixels, not just one. See repo `MEMORY.md` → "Screenshotting the window" for the original
writeup; a working PowerShell + P/Invoke harness (`Capture-Window` etc.) was built ad hoc
during a verification session. Reconstruct similarly if needed: it's straightforward GDI+
P/Invoke.

## The Hard Limitation: Synthetic Input Does Not Reach Any Window Here

**`SendInput`-injected mouse/keyboard events have zero effect in this environment: on
ANY window, not just ToonEngine.** This was proven decisively, not assumed:

- `SendInput` reports success (events accepted, `GetLastError() == 0`).
- `SetForegroundWindow` + `GetForegroundWindow()` agree the target window is foreground.
- Yet a plain **WinForms `TextBox` created in the same PowerShell process**, with
  `.Focused == true` confirmed at the .NET level, received *zero* characters after
  `SendInput`-ing "hello" one key at a time. That test has no cross-process, elevation,
  UIPI, or GLFW-specific variable left to blame. It's conclusive: this session's
  PowerShell has no live input desktop to inject into.
- Consequence: any apparent state change observed on a ToonEngine window during a
  verification session (selection, gizmo mode, checkboxes) that wasn't screenshotted
  immediately before AND after a specific scripted action is **not attributable to the
  script**: it may be the user's own hands on a real keyboard/mouse in their real
  interactive session, running the same freshly-built exe in parallel. Don't assume a
  process you didn't just launch is "yours," and don't trust apparent-but-unverified state
  changes as evidence.
- Don't re-litigate this per session: it cost a long debugging chain (foreground-lock
  theories, elevation/UIPI theories, repeated-Alt-tap theories, cold-start-race theories,
  all individually plausible, all individually ruled out) before the WinForms test made it
  unambiguous. If a future session needs to re-confirm the environment hasn't changed, the
  WinForms `TextBox` test above is the fastest decisive check: a few lines, no process
  launch, no window-discovery ambiguity.

## What to Do Instead, Given the Limitation

Interactive behavior (does hotkey X actually toggle Y when pressed) **cannot be verified
live in this environment**. Fall back to, in order of strength:

1. **Clean build** (`cmake --build`, exit 0): table stakes, not sufficient alone.
2. **API-signature verification against the vendored headers**, not memory/training data.
   For ImGui: `external/imgui/imgui.h`. For ImGuizmo:
   `external/ImGuizmo/src/ImGuizmo.h`. Grep the exact signatures the diff calls
   (`IsKeyPressed`, `Manipulate`'s parameter order, struct fields like `io.KeyCtrl`) and
   confirm every call matches: this is real evidence the code compiles *and* means what
   it looks like it means, not just that clang-cl accepted it.
3. **Static screenshot confirmation**: launch, wait out cold start, screenshot. Confirm
   new UI elements render with correct labels/values/layout. If the session happens to
   observe the target control in more than one state (e.g. a conditional field that
   changes label per mode) across *any* screenshots taken, that cross-validates the
   branching logic even without attributing the transition to a specific action.
4. **Ask the user for a manual smoke test.** They have a real keyboard/mouse in a real
   interactive session: a 30-second manual check on their end is strictly better evidence
   than anything achievable here. Say so plainly rather than presenting (1)-(3) as if they
   were equivalent to a live-driven PASS.

Report this honestly as **BLOCKED** for the interactive-behavior claim specifically (per
the `verify` skill's own guidance: never silently downgrade "couldn't observe it" into a
PASS) while still giving full credit for what (1)-(3) actually established.
