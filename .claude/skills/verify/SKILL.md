---
name: verify
description: Build, launch, and screenshot-verify ToonEngine.exe. Documents a hard environment limitation — synthetic input injection (SendInput) does not reach any window here — and the fallback verification approach that works around it. Use before claiming a UI/interaction change is confirmed working.
---

# verify — ToonEngine build/launch/capture, and the input-injection limitation

## Build + launch (works reliably)

```
cmake --build --preset windows-debug
Start-Process build\windows-debug\ToonEngine.exe
```

Works from a bare shell as long as `build/windows-debug` is already configured: ninja caches
tool paths from the last successful configure, so the VS Developer environment doesn't need
importing for an incremental build. A from-scratch *configure* does need it, and there's no
repo script for this (deliberately; see repo MEMORY.md → "Build gotchas", don't recreate
one). Import it inline for that one command instead:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 -no_logo && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "Env:$($matches[1])" $matches[2] }
}
cmake --preset windows-debug
```

**Cold start is slow on a debug build — wait ~10-12s before trusting any capture.**
First-run HLSL→SPIR-V cross-compilation + Vulkan PSO creation (debug validation layers on)
can block the app past a short retry budget; captures taken too early come back solid white
even though the app is running fine. Sleep ~10s after the window handle appears, *then*
start capturing/driving.

## Screenshotting (works reliably)

Vulkan swap-chain windows are GDI-black via normal screen copy. Use `PrintWindow` with
`PW_RENDERFULLCONTENT = 2`, and make the *capturing* process per-monitor DPI-aware
(`SetProcessDpiAwarenessContext(-4)`) or right-docked panels get cropped on a scaled
display. Retry on an all-white frame (present race / still-compiling) — sample a few
pixels, not just one. See repo `MEMORY.md` → "Screenshotting the window" for the original
writeup; a working PowerShell + P/Invoke harness (`Capture-Window` etc.) was built ad hoc
during a verification session — reconstruct similarly if needed, it's straightforward GDI+
P/Invoke.

## The hard limitation: synthetic input does not reach any window here

**`SendInput`-injected mouse/keyboard events have zero effect in this environment — on
ANY window, not just ToonEngine.** This was proven decisively, not assumed:

- `SendInput` reports success (events accepted, `GetLastError() == 0`).
- `SetForegroundWindow` + `GetForegroundWindow()` agree the target window is foreground.
- Yet a plain **WinForms `TextBox` created in the same PowerShell process**, with
  `.Focused == true` confirmed at the .NET level, received *zero* characters after
  `SendInput`-ing "hello" one key at a time. That test has no cross-process, elevation,
  UIPI, or GLFW-specific variable left to blame — it's conclusive: this session's
  PowerShell has no live input desktop to inject into.
- Consequence: any apparent state change observed on a ToonEngine window during a
  verification session (selection, gizmo mode, checkboxes) that wasn't screenshotted
  immediately before AND after a specific scripted action is **not attributable to the
  script** — it may be the user's own hands on a real keyboard/mouse in their real
  interactive session, running the same freshly-built exe in parallel. Don't assume a
  process you didn't just launch is "yours," and don't trust apparent-but-unverified state
  changes as evidence.
- Don't re-litigate this per session — it cost a long debugging chain (foreground-lock
  theories, elevation/UIPI theories, repeated-Alt-tap theories, cold-start-race theories,
  all individually plausible, all individually ruled out) before the WinForms test made it
  unambiguous. If a future session needs to re-confirm the environment hasn't changed, the
  WinForms `TextBox` test above is the fastest decisive check — a few lines, no process
  launch, no window-discovery ambiguity.

## What to do instead, given the limitation

Interactive behavior (does hotkey X actually toggle Y when pressed) **cannot be verified
live in this environment**. Fall back to, in order of strength:

1. **Clean build** (`cmake --build`, exit 0) — table stakes, not sufficient alone.
2. **API-signature verification against the vendored headers**, not memory/training data.
   For ImGui: `external/DiligentTools/ThirdParty/imgui/imgui.h`. For ImGuizmo:
   `external/ImGuizmo/src/ImGuizmo.h`. Grep the exact signatures the diff calls
   (`IsKeyPressed`, `Manipulate`'s parameter order, struct fields like `io.KeyCtrl`) and
   confirm every call matches — this is real evidence the code compiles *and* means what
   it looks like it means, not just that clang-cl accepted it.
3. **Static screenshot confirmation** — launch, wait out cold start, screenshot. Confirm
   new UI elements render with correct labels/values/layout. If the session happens to
   observe the target control in more than one state (e.g. a conditional field that
   changes label per mode) across *any* screenshots taken, that cross-validates the
   branching logic even without attributing the transition to a specific action.
4. **Ask the user for a manual smoke test.** They have a real keyboard/mouse in a real
   interactive session — a 30-second manual check on their end is strictly better evidence
   than anything achievable here. Say so plainly rather than presenting (1)-(3) as if they
   were equivalent to a live-driven PASS.

Report this honestly as **BLOCKED** for the interactive-behavior claim specifically (per
the `verify` skill's own guidance: never silently downgrade "couldn't observe it" into a
PASS) while still giving full credit for what (1)-(3) actually established.
