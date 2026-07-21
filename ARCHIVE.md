# ToonEngine: Archive

Material that used to live in `MEMORY.md` but no longer earns a place in the
day-to-day lookup path: full historical narratives whose durable conclusions
have already been distilled into a proper section in `MEMORY.md`, and
planning documents whose subject matter has since shipped. Nothing here is
needed to understand or extend the current codebase; it exists for anyone
tracing exactly how a decision or a bug was actually found.

## ImGui Docking: the Original 2026-07-09 Manual-Checkout Approach

Superseded on 2026-07-12 by ToonEngine's own `external/imgui` submodule (see `MEMORY.md`'s
"Docking" section for the durable mechanism). The first working docking build didn't add a
new submodule at all: it manually checked out DiligentTools' own *nested*
`ThirdParty/imgui` submodule to upstream ocornut/imgui's `docking` branch tip, in place,
uncommitted. That worked for the session that made it, but it wasn't durable: DiligentTools
pins that submodule to a specific commit, so a plain `git submodule update --recursive` (the
normal command anyone would run after a fresh clone or a submodule bump) silently reverted
the manual checkout straight back to the non-docking pin, with no error or warning. The
docking build only kept working for as long as nobody ran that command. This is exactly what
the later `external/imgui`-submodule-plus-`DILIGENT_DEAR_IMGUI_PATH`-override approach fixed:
it's a supported override hook DiligentTools' own `ThirdParty/CMakeLists.txt` exposes, so the
docking pin survives every future `submodule update --recursive`, verified by running that
exact command afterward and confirming `external/imgui` stayed pinned and
`external/DiligentTools` stayed clean.

## Temporal Ghosting: the Full Debugging Saga (2026-07-11 to 2026-07-12)

`MEMORY.md`'s "Temporal ghosting fixes" section has the distilled, durable end state: four
independent causes, each with its own fix. This is the full round-by-round journey that
found them, kept for the methodology lesson (each round fixed something real, and still
didn't fully solve it, until the actual root cause was finally read from the algorithm
itself rather than guessed from struct field names and doc comments) rather than for any
technical fact not already captured above.

**Round 1** (user confirmed hotkeys/snap work; found two more issues while testing): gizmo
rotate on a spinning entity silently did nothing (root cause: an `if (spin)`-gated per-frame
spin write had been stomping `rotationEuler` unconditionally); a faint trail followed
move-dragged objects (fix: a new `PostParams::gizmoManipulating` forced SSAO
`ResetAccumulation` during a drag).

**Round 2** (user reported round 1 incomplete): the trail persisted for *rotate* drags too,
and re-enabling Spin after a manual edit snapped back to the old trajectory instead of
continuing from the new orientation. Real fixes: made spin **incremental**
(`rotationEuler += axis*rate*dt`, deleting the shared `spinAngle` clock entirely) so a
paused-then-gizmo-edited orientation is the new baseline it resumes from; extended the same
`gizmoManipulating` reset to **TAA's** `ResetAccumulation` too (previously never set, always
accumulating). Also flagged (not fixed, not a regression): the Helmet's outline has visible
gaps at hard edges, an already-documented limitation of extruding along the plain shading
normal for glTF models, which carry no smooth normal.

**Round 3** (user: round 2 still very present, "pretty much any interaction," including
dragging the Outline-width slider with no gizmo involved at all, shows a fading ghost of the
old width; rotate still keeps the silhouette visibly trailing). The outline-width slider
report was the key clue: it proves the trigger can't be gizmo-specific, since
`ImGuizmo::IsUsing()` is false for a plain Inspector drag. Confirmed the exact mechanism by
reading the outline shaders directly: both `toon_outline.hlsl` and `model_outline.hlsl`
build `CurrClip`/`PrevClip` from the *same* (current-frame) extruded position, varying only
the WorldViewProj, so if only outline width changes (camera + object otherwise static),
`g_WorldViewProj == g_PrevWorldViewProj` and the reported motion is exactly zero even though
the rendered shell visibly grew or shrank. Renamed `PostParams::gizmoManipulating` →
`activeInteraction`, now `ImGuizmo::IsUsing() || ImGui::IsAnyItemActive()`. Also found and
fixed a real gap: SSR has no `ResetAccumulation` field at all (unlike SSAO/TAA), and its own
`TemporalRadianceStabilityFactor` defaults to `1.0`, the most ghosting-prone end of its
documented range; tuned it down to `0.7` defensively.

**Round 4: the actual persistent root cause** (user: ghost is present from startup, before
any interaction at all, and doesn't clear on its own; toggling SSAO's "AO temporal" or the
SSAO master toggle off makes it vanish). This reframed everything: it can't be
interaction-driven, since nothing is interacting at startup. The one thing always running
from frame 1 by default is Spin. Root cause: `toon_outline.hlsl`/`model_outline.hlsl`
computed `PrevClip` by extruding along *this frame's* rotation-derived normal and only
varying the WorldViewProj between curr/prev — exact for pure translation, but the extrude
direction is itself rotation-dependent, so under continuous rotation this always slightly
under-reports motion, every single frame, forever. Fix: added `g_PrevNormalMatrix` to the
shared cbuffer (320→384 B), the inverse-transpose of the *previous* frame's world matrix;
both outline vertex shaders now redo the `PrevClip` extrude with it instead of reusing the
current frame's `inflated` position.

**Round 5** (user: the ghost still appeared at startup even after Round 4's mathematically
correct outline fix). `PostFXContext` had never had a real previous-frame depth buffer, only
the current frame reused as a stand-in (`pPrevDepthBufferSRV = depthSRV`), defeating
depth-based disocclusion entirely for SSAO/TAA/SSR alike. Built a real
`Impl::prevSceneDepth` texture, `CopyTexture`'d from `sceneDepth` at the end of `EndScene`.
Needs the exact same BindFlags as `sceneDepth` or Vulkan validation trips on the SRV's
depth→R32_FLOAT reinterpretation.

**Round 6: actually reading DiligentFX's algorithm** (user confirmed Round 5 didn't fix it
either: the ghost specifically follows Spin, never self-clears). Five rounds of
increasingly-informed guessing from the outside (attribute struct field names, doc
comments) had each fixed something real but never the actual cause, so this round stopped
guessing and read the actual shader source
(`SSAO_ComputeTemporalAccumulation.fx`/`ScreenSpaceAmbientOcclusionStructures.fxh`). Two
findings: (1) `ScreenSpaceAmbientOcclusionAttribs::TemporalStabilityFactor`, the one exposed
"tune the temporal aggressiveness" field, is declared but never read by any SSAO shader —
dead parameter. (2) The real algorithm has a correct depth-based disocclusion check (now fed
properly by Round 5's `prevSceneDepth`) plus a separate motion-magnitude-based variance
safety net, whose tuning constants are compiled-in `#define`s in the vendored shader source,
unreachable from the app. A rotating silhouette is a view-dependent contour: which physical
surface points satisfy "this is the silhouette" changes every frame as the object turns, so
no per-vertex motion vector, however correctly computed, can fully represent it. At Spin's
default rate the residual error is small enough to slip under the safety net's threshold, so
history is heavily trusted and a small per-frame error compounds into a visible, persistent
ghost. Fix: renamed `activeInteraction` → `suppressTemporalHistory`, folding in a third
trigger: `gizmoActive || ImGui::IsAnyItemActive() || spin`. While Spin is on, SSAO/TAA never
accumulate at all.

**Round 7: the camera-motion root cause** (a fresh session, asked to understand the renderer
in depth and fix SSAO "the Diligent way," found a bug none of rounds 1-6 had touched: SSAO/
TAA/SSR ghosting on zoom/orbit/pan, not just Spin). `RunPostFX` fed `PostFXContext` the
*same* camera-attribs instance as both curr and prev — no `prevPostCamera` existed at all —
so the reprojection step's curr→prev round-trip was a no-op regardless of whether the camera
actually moved. During genuine camera motion, a static surface's camera-space depth
legitimately changes frame-to-frame because the camera moved (the reprojection step exists
specifically to cancel that out before comparing); with it disabled, stale history blends in
across a frame where the framing genuinely changed. Camera motion was simply never in any of
the six prior rounds' hypothesis space: all of them looked at object/Spin motion vectors and
`prevSceneDepth`, never at the camera-attribs plumbing itself. Fix: added
`Impl::prevPostCamera`, the same double-buffering pattern already used for
`prevSceneDepth`/`prevViewProj`, snapshotted right after `postFX->Execute()` each frame,
matching `DiligentSamples/Tutorial27_PostProcessing`'s own reference pattern (a real
double-buffered `CameraAttribs[2]`, never aliased).

## Editor UI: The Full Gizmo-Dogfooding Bug Write-Up (2026-07-11)

`MEMORY.md`'s "Editor UI" section now keeps only the durable spin-animation fix, plus a
pointer to "Temporal ghosting fixes" for the ghost-trail cause. This is the original
write-up verbatim, from the session that found all three issues immediately after shipping
gizmo snap + hotkeys (none caused by that change itself; the hotkeys just made dragging
easy enough that this was the first time anyone drove the gizmo hard):

**Bugs found dogfooding the above** (pre-existing, from the original gizmo commit, none
caused by the snap/hotkey change; all surfaced because hotkeys made gizmo-dragging easy
enough that this was the first time someone actually drove it hard):

- **Gizmo rotate silently did nothing on a spinning entity, on any axis, whether Spin was
  ticked or not, but worked fine on the (non-spinning) Ground; and re-enabling Spin after a
  manual edit snapped back to the old trajectory instead of continuing from the new
  orientation.** Root cause: the spin animation was an **absolute** function of one shared
  clock: `for (spinners) e.transform->rotationEuler = axis * spinAngle;`, run
  unconditionally every frame regardless of the `spin` checkbox (only advancing `spinAngle`
  itself was gated). So the frame after any gizmo edit, this stomped `rotationEuler` right
  back to `axis * spinAngle` for every entity in `spinners` (Sphere/Cube/Torus/Helmet, not
  Ground, hence it alone worked); the whole `Vec3` gets replaced, not added to, so *every*
  axis got wiped. And even gated on `if (spin)`, resuming would still snap to wherever the
  shared clock said it "should" be, unrelated to the gizmo-set orientation. **Real fix:
  made the animation incremental instead of absolute**: `rotationEuler = rotationEuler +
  axis * (dt * kSpinRate)` each frame while `spin` is on, so it's always continuing from
  whatever `rotationEuler` currently *is* (a natural continuation, or a gizmo-set baseline)
  rather than recomputing from a shared clock. This let the shared `spinAngle` float be
  deleted entirely. Each entity is now self-contained. Mathematically equivalent to the
  original formula for the untouched default scene (sum of per-frame increments == the old
  closed form), so no visual change there; only a paused-then-edited-then-resumed spinner
  differs, and only in the intended way.
- **A faint trail ("screen burn-in") followed objects while gizmo-dragging them** (both
  move and rotate). `Impl::RunPostFX` feeds `PostFXContext` the current depth buffer as
  *both* curr and prev (`pPrevDepthBufferSRV = depthSRV; // no history, reuse current`, a
  deliberate simplification from the original SSAO work, since nothing needed real depth
  history at the time). That defeats depth-based disocclusion entirely, so both SSAO's
  temporal AO reprojection *and* TAA's color-history accumulation lean solely on motion
  vectors: fine for smooth camera/spin motion, not for a large discontinuous mouse-driven
  jump (not what such reprojection heuristics are tuned for). Fix: a new **app-computed**
  `PostParams::gizmoManipulating` (not a Debug-panel toggle, set from `ImGuizmo::IsUsing()`,
  same 1-frame-lag pattern already used for the camera capture-gate, read at the top of the
  frame before that frame's `Manipulate()` call happens) forces
  `ScreenSpaceAmbientOcclusionAttribs::ResetAccumulation = 1` **and**
  `TemporalAntiAliasingAttribs::ResetAccumulation = true` for the duration of a drag: SSAO
  reuses the exact flag its `ssaoTemporal` off-toggle already sets for the same "no
  ghosting" reason; TAA's was previously never set at all (always `FALSE`, i.e. always
  accumulating, though TAA is off by default, so it likely wasn't the primary contributor
  unless the Debug panel had it toggled on). AO/TAA are very slightly noisier for the
  duration of a drag, then resume smooth accumulation the instant it ends. **Not fully
  confirmed by the user as of the first fix attempt (SSAO-only)**: the TAA half was added
  as a natural extension of the same confirmed-correct root cause, not yet independently
  re-tested. A real fix (an actual double-buffered depth history) is bigger; deferred unless
  this residual trail is still visible after the TAA extension too.
- **The Helmet's outline has visible gaps at hard edges, NOT a regression, already
  documented.** `model_outline.hlsl`'s own header comment (and this file's "glTF model
  loading" section) already states the exact limitation: loaded models carry no smooth
  normal (unlike procedural primitives' `Vertex::smoothNormal`), so the inverted-hull
  outline extrudes along the plain shading normal and gaps at split-vertex hard creases.
  The Helmet's dense mechanical panel lines make this far more visible than on smoother
  models. A real fix needs computing an averaged normal per unique position across the
  loaded glTF vertex buffer (a real geometry-processing task, not a quick patch), worth a
  future roadmap item, not folded into this dogfooding pass.

**Not independently verified interactively by Claude**: this dev environment has no live
input desktop, so synthetic keyboard/mouse (`SendInput`) reaches no window at all, proven
and written up in `.claude/skills/verify/SKILL.md`. Both fixes above were made from a
precise code trace (confirmed correct on read, both root-caused to an exact line), then
confirmed working by the user after a manual test.

## Input System: The CMakeLists.txt Reconfigure Incident and Full Verification Log (2026-07-12)

`MEMORY.md`'s "Input system" section now keeps only a one-line pointer to this incident
(the general lesson lives in "Build gotchas") and a compressed verification summary. This
is the original write-up verbatim:

**A real, non-obvious build gotcha found here, see "Build gotchas" above for the general
lesson now folded in there.** After adding four `target_*` calls to `CMakeLists.txt` (new
sources, the JSON include dir, the `Diligent-JSON` link, two compile defs) in one sitting,
`cmake --build --preset windows-debug` forced a reconfigure and got most of the way through
a full DiligentCore/Tools/FX rebuild before failing on `binding_io.cpp(6,10): fatal error:
'nlohmann/json.hpp' file not found`. The file exists exactly where the new include dir
points (confirmed on disk); the actual cause, found by extracting the real compiler
invocation from `compile_commands.json`, was that **the include dir (and the two new
compile defs) were simply absent from the generated command**: despite `CMakeLists.txt` on
disk having all four edits, confirmed via a fresh `Read` immediately before the build.
Grepping the generated `build.ninja` directly for the new content confirmed zero matches:
the implicit reconfigure genuinely hadn't processed those lines, even though it *had*
picked up the new source-file list (the three new `.cpp`s did compile). Root cause not
fully isolated: `build.ninja`/`compile_commands.json`'s timestamps were only 2 seconds
after `CMakeLists.txt`'s own last-write time, so this reads as the implicit
regenerate-if-stale check running, but CMake's own configure pass not fully applying every
`target_*` call from the edited file. **Fix: an *explicit* `cmake --preset windows-debug`
reconfigure** (not `--build`) picked up all twelve new references immediately (confirmed via
the same `build.ninja` grep), and the subsequent build succeeded.

**Verified:**
- **Clean build** (`cmake --preset windows-debug` then `cmake --build`, exit 0, 663/663
  steps) after the reconfigure fix above.
- **Persistence round-trip: the strongest evidence available without live input.** First
  launch printed `Bindings saved: .../assets/input.json` (the file didn't exist before);
  reading it back confirmed the exact expected schema: `camera.fly.up` bound to E/Q,
  `camera.orbit.x/y` present as gamepad-only axes, no `gizmo.*`/`app.quit` keys anywhere.
  This exercises `RegisterDefaultEditorBindings` → `GetContext` → `BindingIO::Load` (miss)
  → `BindingIO::Save`, the action-map's binding→JSON serialization, and the
  `Diligent-JSON` link, all in one observable artifact, not just "it compiled."
- **Launch + `PrintWindow` screenshot** (cold-start wait, DPI-aware capture, see
  MEMORY.md's "Screenshotting the window" section): the full scene rendered normally at 144
  FPS (helmet, cube+satellite, sphere, torus, ground, gizmo, all panels), confirming the
  moved `BeginFrame` and the new startup load/save path didn't crash or hang. The Debug
  panel's new Camera-section lines rendered correctly, including the conditional
  gamepad-count text, which read as *connected* on this machine. Cross-checked via
  `Get-PnpDevice`: the only matching HID entries are "HID-compliant system controller"
  collections under Razer/keyboard vendor IDs, which look like a peripheral's extra HID
  interface rather than a dedicated controller, reported as an unconfirmed, likely-benign
  detection, not a verified real gamepad.
- **Graceful close**: `CloseMainWindow()` + `WaitForExit` returned within 5s, no hang, no
  abort dialog (the ImGui shutdown-order fix from "Dear ImGui integration" above is
  untouched by this change).
- **Blocked, reported as such rather than glossed over:** live interactive behavior (does a
  held key actually fly the camera, does editing `assets/input.json` change the feel) can't
  be driven synthetically here (`SendInput` reaches no window in this environment, see the
  `verify` skill), and there's no confirmed physical gamepad to test the new stick bindings
  against. Both need a manual check on the user's own machine.

## Verification-Log and Process-Note Trims (2026-07-20 MEMORY.md Compression Pass)

Four verification write-ups and two documentation-sync notes, trimmed from `MEMORY.md`'s
topical sections during the 2026-07-20 bloat pass (the `tidy-md` skill's MEMORY.md/ARCHIVE.md
migration step). Each was already-passing, session-specific corroborating detail rather than
a durable technical fact; `MEMORY.md` keeps a one- or two-sentence compressed version of most
of these in place (the two documentation-sync notes were cut entirely, with nothing kept).
Verbatim originals:

**From "Scene serialization":**

**Verified for real, not just by compiling.** This dev environment has no live input (see
`.claude/skills/verify/SKILL.md`), so the Save/Load buttons can't be click-tested here. A
temporary, reverted self-test in `main.cpp` round-tripped the scripted default scene
through `SaveScene` + `LoadScene` into a throwaway `Scene`/`Camera` at startup (never
touching the live one) and printed a comparison to stderr, redirected to a file at launch.
Confirmed: 8/8 entities round-tripped with correct parent indices (Satellite correctly
came back with `parent=3`, Cube's index), correct positions, a valid regenerated mesh
handle for every primitive, a valid reloaded model handle for Helmet, and Sun reconstructed
with neither mesh nor model set, light only. Also read the written `.scene` file directly:
clean, matches the format above, human-readable. Screenshot-confirmed (before and after
reverting the temp code) that the Debug panel's new Scene section renders correctly and
nothing else regressed.

**From "Asset browser":**

**Verified:** clean build (after the CMakeLists.txt reconfigure gotcha noted in
MEMORY.md's "Asset Browser" section). Screenshot comparison, not just a compile check:
cropped the captured `icon.png` row's thumbnail out of a full-window `PrintWindow` capture
and compared it directly against the source file: same upright orientation, same
brightness, confirming both bug fixes above actually took. Graceful shutdown was also
genuinely exercised despite the no-synthetic-input limitation (see the `verify` skill):
`PostMessage(hwnd, WM_CLOSE, ...)` is a direct Win32 message post, not `SendInput`-based
injection, so it isn't subject to that limitation: GLFW's win32 backend handles `WM_CLOSE`
in its window procedure regardless of focus state. The process exited cleanly in ~2s with
nothing in the Application event log: stronger evidence than one captured frame rendering
fine, since it confirms `FileBrowser::Shutdown` and the new `Impl::textures.clear()` are
ordered correctly across teardown. **Still blocked:** clicking a row to check the preview
pane, double-clicking a folder to navigate, and double-clicking a `.scene` file to confirm
the load all need synthetic input this environment doesn't have. The last one is also
untestable for an unrelated reason: no `.scene` file exists yet in a fresh `assets/scenes/`
(nobody has clicked "Save Scene" in this build), so even a manual check needs that done
first.

**From "Entity behavior system":**

Verified non-interactively (no synthetic input reaches this environment; see the
`verify` skill): clean build. A temporary default-to-`Playing` build captured two
screenshots 5s apart and showed the Cube's rotation advance from
`(123.186°, 246.372°, 0°)` to `(212.856°, 425.712°, 0°)`, an exact 2:1 X:Y ratio matching
its `{0.5, 1.0, 0}` axis and a magnitude consistent with 0.6 rad/s given normal
wall-clock capture slop, with the visual cube, its shadow, and the parented Satellite all
rotating in the screenshots too. A second temporary block (removed after use, like the
first) exercised the copy constructor and the save/load round-trip directly by calling
them from `main()` and dumping results to stderr: the copy produced a *different* script
pointer with *identical* field values (a genuine deep clone, not aliased), and a
save-then-load round trip preserved all 8 entities including the Cube's script and its
exact field values. Both temporary instrumentation blocks were fully removed and the
final build reconfirmed clean (identical warning count to the pre-instrumentation build).

**From "Entity behavior system" (documentation-sync note, cut entirely from MEMORY.md):**

Docs sync done in a follow-up `tidy-md` pass, not folded into the implementation
session: pruned the M1 roadmap entry out of CLAUDE.md, added `core/script.{h,cpp}` +
`core/scripts/` to its source layout (attempted inline during implementation, but
reverted then, since it pushed the file 2 lines past its hard 200-line cap; the `tidy-md`
pass found a line to trim instead), and rewrote `docs/architecture.md`'s "Where new
systems plug in" from speculative future tense into a descriptive account of what
actually got built, plus a new "Scripts" subsection under "The scene model" documenting
the `Entity`-copy-constructor consequence. README's Highlights gained a native-scripting
bullet.

**From "Physics + collision" (documentation-sync note, cut entirely from MEMORY.md):**

Docs sync folded into this same `tidy-md` pass: pruned the M2.1 roadmap entry out of
CLAUDE.md (folding its shipped capabilities into Current State instead), added its two named
follow-ups (mouse-pick raycast, contact events → scripts) to the M2 roadmap list, added
`core/physics.{h,cpp}` to CLAUDE.md's and `docs/architecture.md`'s source layouts, and
rewrote `docs/architecture.md`'s "Where new systems plug in" physics paragraph from
speculative future tense into a descriptive account of what actually got built (plus a new
"The physics abstraction layer" section, `Transform`'s vocabulary entry, the `Entity` struct's two new
fields, the frame loop's physics step, and the Play/Stop section, all updated to match).
README's Highlights gained a physics bullet.

## ToonEngineOld: the Original Carry-Over Survey (2026-07-10, Audited 2026-07-11)

The full per-system porting plan written before any of the engine/editor-layer roadmap items
had shipped. Every system named here as "to port" has since shipped and has its own section
in `MEMORY.md` (scene graph, editor camera + input, gizmos, serialization, the file browser/
thumbnails/themes, glTF loading, cascaded shadow maps); `MEMORY.md`'s "ToonEngineOld:
carry-over reference" section keeps only what's still unshipped (grid/sky, sprites,
skeletal animation) and the shader hot-reload recipe. Kept here for the original reasoning
and scope decisions behind each port.

**Carry-over map** (per system, as originally surveyed):
- **assets**: fonts (BaiJamjuree, OpenSans), 4 test models, icon: copied into `assets/`
  (models via Git LFS). The old GLSL shaders stayed as HLSL-port references: `toon.frag`
  (spec + rim + shadow ramp, richer than the fill that shipped), `grid.frag`, `shadow.*`.
- **scene/scene.{h,cpp}**: entity tree (flat vector + parent index, root at 0, cached world
  matrices, add/delete/reparent/duplicate, world-preserving reparent). High value; port
  glm→Diligent math and old handles→`MeshHandle`/`Material`.
- **scene/model_loader**: cgltf (glTF) + ufbx (FBX) → meshes/materials/skeleton/anim.
  Decision made at the time: use DiligentTools' glTF loader instead (native integration) →
  glTF/GLB only, no FBX (`dragon.fbx` won't load via that path; `dragon.gltf` does). Keep the
  old loader as the reference if FBX/skeleton parsing is ever wanted.
- **ui/overlay**: inspector + `RenderSettings` (bands, spec, rim, shadow ramp, outline incl.
  a screen-space-width flag, CSM, grid, sky, gizmo) + ImGuizmo transform gizmos.
- **scene/camera**: orbit/pan/zoom/fly/focus editor camera (replaced the original turntable).
- **core/input/**: keyboard/mouse/gamepad + action maps + rebinding + an ImGui capture gate;
  GLFW-based, largely direct.
- **core/animator + animation**: skeleton + keyframe clips; deferred until skinned loading.
- **ui/file_browser + themes + thumbnail_cache**: asset browser + 3 themes + texture/model
  preview thumbnails; mostly portable ImGui. Thumbnails were planned to decode/upload through
  the already-linked `Diligent-TextureLoader`, not a new image lib (and did, when it shipped).
- **core/renderer (GL) + main.cpp**: reference only, never ported.

**Materials will need textures**: the old `Material` was `baseColor + texture + normalMap`,
and loaded models carry albedo/normal maps, so the plan called for texture handles on the
abstraction layer + a textured cel fill, and UVs on the toon `Vertex` (bone weights later).
This shipped as "glTF model loading."

### Diligent Overlap Check (2026-07-11)

Before adding the input-system/asset-browser/fixed-timestep/shader-hot-reload items to the
roadmap, each was checked against the guiding principle (build *on* Diligent, don't reinvent
it), against the actual vendored source (DiligentCore/Tools/FX only; DiligentSamples is not
a submodule here) plus Diligent's own docs/blog:

- **Input/camera controllers.** Genuinely nothing to build on: DiligentCore and
  DiligentTools have no windowing or input abstraction at all. The only `*Camera*` hit in
  either (grepped both trees) is `NativeApp/Android/ndk_helper/tapCamera.h`, Android-only.
  `FirstPersonCamera`/`InputController` exist only in DiligentSamples, a separate repo
  ToonEngine doesn't vendor, and Diligent's own docs confirm the engine "does not define any
  platform-specific window abstraction." So hand-rolling input/camera wasn't a
  guiding-principle violation: there was no in-scope Diligent equivalent to defer to.
- **Fixed timestep.** Not a Diligent concern either way: `Timer.hpp` is a bare
  `std::chrono` stopwatch, not a fixed-timestep/accumulator solution. The accumulator +
  decoupled sim-rate pattern is pure game-loop architecture, orthogonal to the graphics API.
  Shipped later as "Fixed timestep + render interpolation" (M1.1).
- **Asset thumbnails.** Reuse the texture path already on the abstraction layer:
  `Diligent-TextureLoader`'s `CreateTextureFromFile` (already linked for model textures) is
  the right entry point, per the old engine's `thumbnail_cache.{h,cpp}`. Shipped as part of
  "Asset browser."
- **Shader hot-reload.** Diligent already has this (`Diligent::IRenderStateCache`); the full
  recipe is preserved in `MEMORY.md`'s "ToonEngineOld: carry-over reference" section since
  this item hasn't shipped yet.

## Full Chronological History (Original Log)

This is the complete, unedited chronological ship log that used to be `MEMORY.md`'s
"History" section, verbatim. `MEMORY.md` now carries only a compact one-line-per-entry
version of this same timeline, pointing at each feature's topical section. Nothing here
needs to be read for day-to-day engineering; it exists purely as a complete record for
anyone who wants the session-by-session narrative behind a specific date.

- **2026-07-06**: Pivoted from a from-scratch OpenGL 4.1 engine (see `main`
  branch history) to Diligent Engine + Vulkan on the `diligent` branch.
  Verified first light: window + Vulkan device + swap chain + clear loop,
  running on an NVIDIA RTX 3080.
- **2026-07-08**: Added the renderer's abstraction layer (`core/renderer.h/.cpp`); `main.cpp`
  became Diligent-free. Added DiligentTools + Dear ImGui behind it;
  fixed the C-language, ShowDemoWindow-link, and ImGui-ordering issues above;
  disabled D3D11/D3D12/OpenGL to cut build time.
- **2026-07-08**: Toon pipeline first light: banded (cel) fill +
  inverted-hull outline on a spinning UV sphere, with live ImGui controls.
  Added `core/math.h` (Diligent-free vectors), `core/primitives.{h,cpp}`
  (UV-sphere generator), and `assets/shaders/` (HLSL). Extended the abstraction layer with
  `CreateMesh` / `SetCamera` / `SetToonParams` / `DrawMesh`. Verified the render
  via `PrintWindow` capture on the RTX 3080 (see *Toon pipeline* for the
  matrix/winding/outline conventions nailed down here).
- **2026-07-09**: Toon pipeline refinements: multi-object scene (sphere + cube
  + torus) with per-object `Material` (replaced global `SetToonParams`;
  `DrawMesh` now takes a material, light is global via `SetLight`). Added
  `MakeCube`/`MakeTorus` and the dual-normal outline (`Vertex::smoothNormal`)
  so the cube's hard edges outline cleanly. Nailed the left-handed winding
  gotcha (cube corners). Verified all three shapes on the RTX 3080.
- **2026-07-09**: imgui docking (roadmap #5): see-through dock space, debug
  panel docked left by default, driven from `main.cpp` and guarded on
  `IMGUI_HAS_DOCK`. Required checking out the nested imgui submodule to upstream
  ocornut/imgui's `docking` branch: the DiligentGraphics fork's docking branch
  is ancient/incompatible. That checkout was manual and uncommitted (reverted by
  any `git submodule update --recursive`): superseded 2026-07-12 by a dedicated
  `external/imgui` submodule; see "Docking" in MEMORY.md for the current mechanism.
- **2026-07-09**: DiligentFX (roadmap #6, in progress): added the submodule
  (API256018) + build wiring, and stood up the HDR pipeline: offscreen RGBA16F
  scene target resolved to the back buffer by an exposure + ACES tone-map pass
  (`Renderer::EndScene`, `tonemap.hlsl`). Foundation for DiligentFX bloom/SSAO
  next. See "DiligentFX / HDR post-processing" in MEMORY.md.
- **2026-07-10**: Tooling: migrated the IDE from VS Code to **CLion**. Removed
  `.vscode/` (tasks/launch/settings/c_cpp_properties) and added
  **`docs/clion-setup.md`** (Visual Studio toolchain + CMake presets + debug). The
  CLion VS toolchain sources the VS Developer environment automatically, so
  `scripts/vsenv.ps1` is now only for command-line / CI builds. Trimmed `CLAUDE.md`
  to a lean, forward-only roadmap: completed items live here in the archive.
  (Later split into per-platform `docs/clion-setup-{windows,linux,macos}.md`.)
- **2026-07-10**: **Bloom** (roadmap #1): wired DiligentFX's `Bloom` via
  `PostFXContext` onto the HDR target. `Impl::RunBloom` in `EndScene` prepares +
  executes PostFXContext (fed scene depth as curr/prev, a zero motion-vector target,
  a zeroed camera, scaffolding it needs to reach `IsPSOsReady()` but Bloom never
  reads) then Bloom over `hdrColor`; the tone-map then resolves Bloom's output, which
  already holds scene+glow (so `tonemap.hlsl` is unchanged). `PostParams` + UI gained
  bloom controls; default threshold is 0.6 (the LDR toon fill never exceeds ~0.9).
  Also `DILIGENT_NO_RADIENT ON` (broke the full `cmake --build`) and a new include
  dir for DiligentFX's C++-side `*Structures.fxh`. Built clean (clang-cl) and ran
  with zero Diligent validation errors. See "Bloom" in MEMORY.md.
- **2026-07-10**: Bloom bugfixes + cleanup. (1) `g_HDRColor` MUTABLE-to-**DYNAMIC**:
  the per-frame scene/bloom re-`Set` was tripping `VerifyResourceBinding`; killed the
  cache + `BindPostInput`. (2) **ImGui shutdown order**: `ImGui_ImplGlfw_Shutdown()`
  before context destroy, else `abort()` on window close ("Forgot to shutdown Platform
  backend?"); also built the ImGui PSO with depth = `TEX_FORMAT_UNKNOWN` (kills a
  per-frame DSV-mismatch warning) and `WaitForIdle()` before teardown. Verified via a
  close test (exit 0). (3) Added section dividers to `renderer.cpp`, plus
  **`docs/style-guide.md`** (renamed `docs/cpp-style-guide.md` on 2026-07-11) and a
  **`.claude/skills/tidy-cpp`** skill for future cleanups. See the Dear ImGui + Bloom
  "Gotchas" in MEMORY.md.
- **2026-07-10**: **SSAO** (roadmap #1): DiligentFX `ScreenSpaceAmbientOcclusion` via
  the shared `PostFXContext`, which now gets *real* inputs (unlike Bloom): a
  world-space **normal G-buffer** (scene pass is now MRT; toon shaders write
  `PSOutput` color+normal) and real **`CameraAttribs`** (`FillCameraAttribs`, handness
  from the view determinant). Motion stays zero, so SSAO temporal accumulation is off
  by default (would ghost the spinning scene). AO (visibility) composited in the
  tone-map as `hdr *= lerp(1, ao*ao, strength)`; `g_AO` dynamic, 1x1-white default when
  off. Added a **ground plane** (`MakePlane`) so contact shadows are visible; verified
  the raw AO buffer (torus hole dark, bg white, correct orientation), ran clean, close
  exits 0. `RunBloom` generalized to `RunPostFX`. See "SSAO" in MEMORY.md.
- **2026-07-10**: **Motion vectors** (unblocks SSAO temporal + DoF): the scene now
  writes a real NDC velocity buffer (3rd MRT target) instead of a zero texture. Toon
  shaders difference `currClip`/`prevClip`; `DrawMesh` gained a `prevTransform` and
  `SetCamera` snapshots `prevViewProj`. SSAO temporal accumulation now **on by
  default** (denoises without ghosting). Convention: `currNDC - prevNDC`, raw (the lib
  applies the NDC-to-UV (0.5,-0.5) scale). Verified the motion buffer directly (static =
  black, spinning = rotational red/green). See "Motion vectors" in MEMORY.md.
- **2026-07-10**: **Depth of field** (roadmap #1): DiligentFX `DepthOfField` via the
  shared context, using the new motion vectors for temporal CoC smoothing. `RunPostFX`
  became a color chain (scene, DoF, Bloom, returns `colorOut`); focus/aperture set
  in `CameraAttribs` from `PostParams`. Off by default (strong look); tuned defaults
  (focus 10.5, f/6). Verified: clean run, graceful close, visible depth blur (cube in
  focus, bokeh elsewhere). See "Depth of field" in MEMORY.md.
- **2026-07-10**: **TAA** (roadmap #1): DiligentFX `TemporalAntiAliasing`, first in the
  color chain. `SetCamera` jitters the projection (`GetJitteredProjMatrix`) when TAA is
  on and records `f2Jitter`; `main.cpp` now sets post params before `SetCamera`. Off by
  default (softens toon edges). Verified: clean run, graceful close, spinning objects
  anti-alias without ghosting (motion+jitter correct). See "TAA" in MEMORY.md.
- **2026-07-10**: **SSR** (roadmap #1, the last DiligentFX effect): DiligentFX
  `ScreenSpaceReflection`. Roughness packed into the normal buffer's `.w`
  (`Material::roughness`, `RoughnessChannel = 3`); reflection radiance composited in
  the tone-map (`g_SSR`, simplified, no PBR BRDF/env-map). `RunPostFX` now returns
  `(colorOut, aoOut, ssrOut)`. Off by default; objects made lightly glossy (0.15) so
  it's visible when enabled (a flat ground reflects the sky, mostly misses). Verified
  via the radiance buffer; clean run, graceful close. See "SSR" in MEMORY.md. **All six
  DiligentFX post effects (Bloom, SSAO, DoF, motion vectors, TAA, SSR) are now in.**
- **2026-07-10**: **Non-uniform scale** (roadmap #1, toon pipeline extensions): added an
  inverse-transpose **normal matrix** (`g_NormalMatrix`) so the fill shading and the
  normal/roughness G-buffer stay correct under non-uniform `Transform::scale`, and reworked
  the inverted-hull outline to extrude a uniform **world-space** width (reusing the WVP path
  via the 3x3 transpose of the normal matrix = world-inverse). One added cbuffer matrix
  (256 to 320 B); both changes reduce algebraically to the old behavior at scale = 1, so the
  existing scene is unchanged. Demo: the sphere is now a non-uniformly-scaled spinning
  **ellipsoid** (`Object` gained a per-object `scale`). Built clean (clang-cl), ran with
  zero validation errors, graceful close; verified the ellipsoid shading + uniform outline
  via `PrintWindow`. See "Non-uniform scale" in MEMORY.md.
- **2026-07-10**: **Per-object outline tuning** (roadmap #1): stopped `main.cpp` stomping
  each object's outline with a shared `style`: every `Object` now carries its own outline
  color + width (sphere thin dark-red, cube bold near-black, torus dark-bronze), the draw
  loop overlays only global band/ambient/gloss onto a per-draw material copy, and a global
  `outlineScale` scales all widths together. UI reworked into a per-object "Objects" section
  + a global "Outline width x" multiplier (`Object` gained a `name`). App-only: the
  Material/shader already carried per-object outlines. Built clean, verified three distinct
  outlines via `PrintWindow`. See "Per-object outline tuning" in MEMORY.md.
- **2026-07-10**: **Roadmap redesign + ToonEngineOld carry-over.** With the renderer core
  done, pivoted the roadmap from "more rendering" to the **engine/editor layer** (phases:
  real assets, scene graph, editor UI, environment, animation/2D; instancing deferred),
  porting `ToonEngineOld`'s systems onto the Vulkan abstraction layer. Surveyed the old engine
  (untracked reference folder) and copied its portable assets (fonts, 4 test models, icon)
  into `assets/`: models via **Git LFS** (`.gitattributes` tracks `assets/models/**`). Model
  loading will use **DiligentTools' glTF loader** (glTF/GLB only; the old cgltf/ufbx loader
  is the FBX reference). Next up: Phase A, textured materials + load/cel-shade a real
  model. See "ToonEngineOld: the original carry-over survey" above. Also **codified the
  build-on-Diligent principle** in CLAUDE.md (use Diligent's own implementations, loaders,
  FX, ImGui; the abstraction layer only tames boilerplate + keeps the app/public API
  backend-agnostic, never 1:1 abstraction) and generalized the abstraction-layer rule:
  Diligent lives in the engine's implementation TUs, not just `renderer.cpp`: only the app
  layer + public headers stay Diligent-free.
- **2026-07-10**: **glTF model loading** (Phase A / "real assets"): load + cel-shade real
  models via DiligentTools' `GLTF::Model` (Diligent-first, no hand-rolled loader, no PBR
  renderer). New abstraction-layer `ModelHandle` / `LoadModel` / `DrawModel`; `model_fill.hlsl` reuses the
  toon CB + `CelShade` helper; `helmet.glb` renders textured + cel-shaded in the HDR/post
  pipeline. Linked `Diligent-AssetLoader` + `Diligent-TextureLoader`; baked `TOON_MODELS_DIR`.
  Four loader gotchas cost cycles (VertBufferBindFlags = BIND_NONE default; textures are
  Texture2DArray; the buffer/texture getters need device+context; a dimension-mismatch
  assertion HANGS and logs to buffered cout). See "glTF model loading" in MEMORY.md. Verified
  on the RTX 3080 via `PrintWindow` (clean run, graceful exit).
- **2026-07-10**: **Model outline**: models get the inverted-hull silhouette too, via
  `model_outline.hlsl` (extrude along the shading normal, no smooth normal) + a cull-FRONT
  outline PSO; `DrawModel` draws outline then fill per primitive. The helmet now matches the
  toon look. See "glTF model loading" in MEMORY.md.
- **2026-07-10**: **Scene graph** (Phase B): `core/scene.{h,cpp}`: an entity tree with
  hierarchy-composed world matrices; the render loop walks the scene instead of a hardcoded
  array. Design call: `scene.cpp` is a Diligent-using TU (composition via `float4x4`, no
  reinvented 4x4 math) and `math.h` gained a plain `Mat4` (abstraction-layer vocabulary) with new `Mat4`
  `DrawMesh`/`DrawModel` overloads; the `Transform` overloads delegate. Motion vectors now
  come from the scene's double-buffered world matrices (no prev-angle bookkeeping). Demo: a
  satellite parented to the cube orbits it. Editor-triggered mutations (reparent, duplicate,
  decompose, selection) deferred to the editor step. Built clean, verified via
  `PrintWindow`. See "Scene graph" in MEMORY.md.
- **2026-07-10**: **Editor camera + input** (Phase B, item 4): an orbit-around-pivot camera
  (extends the LH turntable: `SetCamera` prepends `Translation(-pivot)`; did NOT port glm's
  RH lookAt) + `core/camera.{h,cpp}` controls (orbit/pan/zoom/fly/focus; basis from the same
  Diligent rotations as the view) + `core/input.{h,cpp}` (GLFW polling, scroll callback
  chained by ImGui, capture gate from `io.WantCapture`). Right-drag orbit / mid-drag pan /
  scroll zoom / WASD fly / F focus, suppressed over the UI. Action-map/rebinding deferred.
  Built clean; static render verified via `PrintWindow` (drag feel is interactive-only). See
  "Editor camera + input" in MEMORY.md.
- **2026-07-11**: **Gizmo snap + hotkeys** (roadmap A.1 follow-up, closing out the item that
  shipped gizmos + world-preserving reparent): **W/E/R** switch move/rotate/scale, **X** toggles
  local/world (edge-triggered `ImGui::IsKeyPressed`, gated on not-typing / not-flying), and
  **snap** (checkbox or held Ctrl) feeds ImGuizmo's per-op `snap` param with editable
  translate/rotate/scale step sizes. Resolved the "WASD is taken by the camera fly" deferral
  by noticing the fly only runs while right-mouse is held. `main.cpp`-only (no
  abstraction-layer/renderer/shader/input-layer change). See "Gizmo snap + hotkeys" in
  MEMORY.md.
- **2026-07-11**: **Dogfooding bugfixes** found immediately after shipping the above (all
  pre-existing, from the original gizmo commit, not the snap/hotkey change itself). Also
  discovered and documented, the hard way, that this dev environment has **no live input
  desktop**: `SendInput` reports success and focus APIs agree, but nothing actually receives
  synthetic keyboard/mouse, proven decisively with an isolated WinForms textbox test.
  Interactive UI/gizmo verification is therefore not possible from Claude here; every fix
  below was code-traced to an exact root cause and reported back by the user manually.
  Written up for reuse in `.claude/skills/verify/SKILL.md`. See "Temporal ghosting: the full
  debugging saga" above for Rounds 1-7 of the ghosting portion of this work.
- **2026-07-11**: **Tooling correction: `scripts/vsenv.ps1` should not exist.** A session
  building a `tidy-md` doc-maintenance skill + LSP setup found CLAUDE.md, this file, and the
  `verify` skill all describing `scripts/vsenv.ps1` as if it were present, confirmed it
  wasn't (twice), and wrongly concluded the file was the bug, then recreated it. It wasn't:
  the user had deliberately deleted it as vestigial from the pre-CLion VS Code
  workflow (see the 2026-07-10 CLion-migration entry above) and explicitly did not want it
  recreated. Corrected by deleting the file again and fixing every doc that referenced it
  (CLAUDE.md, MEMORY.md's "Build gotchas", the `verify` and `tidy-md` skills, `.clangd`,
  `docs/clion-setup-windows.md`, `README.md`) to describe the VS-environment import as an
  inline snippet or the stock "Developer PowerShell for VS 2022" shortcut instead of a repo
  script. General lesson (folded into the `tidy-md` skill): a doc referencing a missing file
  is stale in one of *two* directions: the file may need restoring, or the doc may need to
  stop claiming it exists, check which before acting, don't assume the first.
- **2026-07-11**: **Light entity component** (the light piece of roadmap A.1's
  "light/sprite/animation entity components"): promoted the global `lightDir` + Debug-panel
  slider to a `Sun` scene entity, aimed by rotation via a new `MakeLightTransform`/
  `GetActiveLight` pair in `scene.cpp`, with editable color/intensity (`LightComponent`)
  premultiplied into a new `g_LightColor` cbuffer field (384 to 400 B) that the two fill
  shaders multiply in. Reproduces the old default direction and look exactly; single-light
  scope (first entity found) by design. Sprite/animation entity components remain, deferred
  to roadmap phase C. Verified via build + screenshot: a regression check, a blue-tinted
  spot-check proving the shader path actually runs, and a clean console log. See "Light
  entity component" in MEMORY.md.
- **2026-07-11**: **Roadmap audit: ToonEngineOld's own CLAUDE.md TODO lists.** Diffed
  `ToonEngineOld/CLAUDE.md`'s "Engine Roadmap TODO" / "ImGui TODO" lists (the old engine's
  own unshipped wishlist: distinct from the proven systems it actually built, which the
  carry-over survey above already covers) against the current roadmap. Folded in four
  concrete, verified gaps as new CLAUDE.md roadmap items: an explicit **input system**
  bullet (already noted as deferred here, but never promoted to CLAUDE.md's forward
  roadmap), an **asset browser panel** bullet (`ui/file_browser` + the previously-unlisted
  `ui/thumbnail_cache`), a **fixed-timestep** game-loop bullet (`main.cpp` currently runs a
  plain variable `dt`), and a **shader hot-reload** bullet wired explicitly to Diligent's
  own `IRenderStateCache` rather than a hand-rolled file-watcher. Skipped the speculative
  half of the old lists (audio, physics, particles, undo/redo, material editor, drag-drop
  material/model workflows, render-stats/profiling panel, status bar, shortcuts overlay,
  animation blending, morph targets): no code or design work backs any of them yet,
  unlike the four folded in. See "Diligent overlap check" above for the per-item
  guiding-principle verification (checked against the actual vendored source, since
  DiligentSamples, where Diligent's own camera/input helpers actually live, isn't a
  submodule here).
- **2026-07-12**: **Scene serialization** (roadmap A.2, now shipped, see "Scene
  serialization" in MEMORY.md for the full writeup). `core/serializer.{h,cpp}`: `SaveScene`/
  `LoadScene` to a line-based text `.scene` file covering the camera and every entity's
  hierarchy, transform, material, and light. Required extending `Entity` with
  `PrimitiveDesc primitive` + `std::string modelPath` so procedural meshes (which have no
  source file, unlike a loaded model) can regenerate on load instead of just carrying a
  live GPU handle that a fresh process can't reconstruct. Deliberately scoped to camera +
  entities, not `PostParams`/style/theme/Spin, which are editor tuning, not scene content.
  Verified with a temporary, reverted self-test (no live input here to click the actual
  buttons, see the `verify` skill) that round-tripped the scripted default scene end to
  end: 8/8 entities, correct hierarchy, valid regenerated mesh/model handles.
- **2026-07-12**: **Window icon (taskbar fix)**: see "Window icon: the taskbar needs an
  embedded resource" in MEMORY.md for the full writeup. The user added `SetWindowIcon`
  (`glfwSetWindowIcon` via `WM_SETICON`) separately; it fixed the title bar but not the
  taskbar/Alt-Tab/shell, which GLFW's Win32 backend drives from a `GLFW_ICON`-named
  resource embedded in the .exe, not runtime state. Added `src/icon.rc.in` +
  `assets/icon.ico` (hand-built: a 22-byte ICO header prepended to the existing PNG's
  bytes) + `CMakeLists.txt` wiring (`enable_language(RC)`, `configure_file`,
  `target_sources`, all `WIN32`-guarded). Verified by screenshot: captured the taskbar
  itself (`Shell_TrayWnd`, via ordinary `CopyFromScreen` since it isn't a Vulkan swap-chain
  surface) and confirmed the real icon renders there now.
- **2026-07-12**: **Durable docking fix** (closes the item CLAUDE.md's roadmap listed as
  "fork DiligentTools, pin imgui to a `docking` commit", see "Docking" in MEMORY.md for the
  mechanism actually shipped, which is lighter than that). Added ToonEngine's own
  `external/imgui` submodule (`branch = docking`, pinned to the same upstream ocornut/imgui
  commit, `a23e9fb1b`, 1.92.9-WIP, the manual checkout used) and pointed
  `DILIGENT_DEAR_IMGUI_PATH` at it in `CMakeLists.txt`, before
  `add_subdirectory(external/DiligentTools)`. DiligentTools itself is untouched, not
  forked: its `ThirdParty/CMakeLists.txt` only defaults that path `if (NOT
  DILIGENT_DEAR_IMGUI_PATH)`, so the override wins and DiligentTools builds from its
  pristine upstream state (its own vendored `ThirdParty/imgui` is initialized but unused).
  Chosen over forking DiligentTools to avoid rebasing a fork every time DiligentTools is
  bumped. Verified end-to-end: a fresh `cmake --preset windows-debug` +
  `cmake --build --preset windows-debug` built clean (680 steps, the first CLI build under
  this preset dir), a launch + `PrintWindow` screenshot showed the real DockBuilder split
  layout (Scene Hierarchy left, Inspector + Debug right, scene visible through the
  pass-through center, not floating windows), and re-running `git submodule update --init
  --recursive` afterward left `external/imgui` pinned and `external/DiligentTools` clean:
  the exact command that used to silently revert docking now leaves it intact.
- **2026-07-12**: **Input system** (roadmap A.1, now shipped, see "Input system" in
  MEMORY.md for the full writeup). Ported `ToonEngineOld/src/core/input/` into `core/input/`: action
  maps (FNV-1a hashed, keyboard/mouse/gamepad bindings, an axis type merging keyboard +
  gamepad into one named value), an input-context stack, and JSON-bound rebinding
  (`assets/input.json`, via the already-vendored `Diligent-JSON`, had to be linked
  explicitly, since `Diligent-AssetLoader` links it `PRIVATE`). Replaces the minimal
  polling-only `core/input.{h,cpp}`. Checked against the guiding principle first (nothing
  in DiligentCore/Tools to build on; DiligentSamples' `InputController` isn't vendored:
  already established in "Diligent overlap check"). `main.cpp`'s camera controls now split
  cleanly between raw mouse-drag polling (orbit/pan/zoom, unchanged) and the new action map
  (fly axes + focus, gated on `WantCaptureKeyboard` since the action queries bypass the
  capture gate by design); gamepad right-stick orbit is a genuinely new capability. Found
  and fixed a real build-system gotcha along the way: `cmake --build`'s implicit reconfigure
  can silently under-apply a multi-call `CMakeLists.txt` edit: an explicit
  `cmake --preset` resolved it (see "Build gotchas" in MEMORY.md). Verified: clean build (663/663
  steps), a generated `assets/input.json` matching the exact expected binding schema, a
  clean launch/render/graceful-close via screenshot, and honestly-reported limits (no live
  input desktop here to drive interactively, no confirmed physical gamepad to test the new
  stick bindings against).
- **2026-07-12**: **Round 7: the actual camera-motion root cause.** See "Temporal ghosting:
  the full debugging saga" above for the complete writeup.
- **2026-07-13**: **Asset browser panel shipped (roadmap A.1: the last engine/editor-layer
  carry-over item).** Full writeup under "Asset browser" in MEMORY.md. Headline points: added a
  texture API to the abstraction layer (`LoadTexture`/`DestroyTexture`/`GetTextureImGuiID`/
  `GetTextureSize`) since the current data-encapsulated `Renderer` had none, unlike the
  free-function abstraction layer the old `ui/file_browser`/`thumbnail_cache` reference was
  written against; caught two bugs
  the GL reference would have carried over silently (`IsSRGB` must be `false` or thumbnails
  render dark, and the reference's GL-bottom-origin UV flip must be dropped for Vulkan's
  top-origin decode) by comparing a captured thumbnail against its source file, not by
  inspection; and confirmed graceful shutdown genuinely exercises the new cleanup path via a
  direct `WM_CLOSE` post (sidesteps the no-synthetic-input limitation, since it's a Win32
  message post rather than `SendInput`). Hit the CMakeLists.txt reconfigure gotcha again
  (new source + new define forced the VS-env-import-must-be-chained-with-the-build failure).
  **Blocked:** click-to-preview, double-click-navigate, and double-click-to-load-scene all
  need a manual check: no live input desktop, and no `.scene` file exists yet to test the
  last one against.
- **2026-07-13**: **ToonEngineOld triage, roadmap reframed around a playable game,
  docs/architecture.md added.** Audited `ToonEngineOld` in full against this file's own
  carry-over map: its `CLAUDE.md` had zero salvage value left (a subset of the carry-over
  survey, its TODO lists already dispositioned on 2026-07-11), so the folder stays only for
  the un-shipped systems' source (grid/sky, sprites, skeletal animation, cascaded shadow
  maps turned out to already be shipped, see the entry right below this one). Reworked
  `CLAUDE.md`'s roadmap: it was rendering-fidelity-and-infra-only and didn't answer
  "what's needed to build a game," so it's now milestone-based and dependency-sequenced
  (M1 simulation foundation: fixed timestep, play mode, entity behavior; M2 physics + audio;
  M3 characters/fidelity: the ToonEngineOld ports; M4 scale/polish), also fixing a garbled
  line from a prior edit. Trimmed CLAUDE.md's "Current state" prose to stay under its
  200-line cap now that the deep version lives in the new `docs/architecture.md`: an
  11-section onboarding doc (abstraction layer, source layout, frame loop, rendering pipeline, scene
  model, editor layer, data flow/ownership, build/dependencies) built from a fresh full-repo
  architecture pass. Repointed README's "full architecture writeup" line at the new doc, and
  extended the `tidy-md` skill to actively maintain `architecture.md` (it was going to fall
  into `docs/**`'s passive "touch only if stale" bucket, which would have let it drift silently
  as the code changes rather than the roadmap/docs).
- **2026-07-13**: **Cascaded shadow maps shipped** (was M3 roadmap item, listed as
  "un-shipped" in the ToonEngineOld triage entry just above, that was written earlier the
  same day; superseded by this). Directional shadows from the scene light onto every
  cel-shaded surface, via Diligent's own `ShadowMapManager` (`external/DiligentFX/Components`):
  cascade distribution, the shadow-map atlas, and cascade selection/PCF sampling
  (`Shaders/Common/public/Shadows.fxh`) are all Diligent's, not hand-rolled, per the guiding
  principle. 4 cascades, 2048^2 D32_FLOAT, PCF (not VSM/EVSM, cheap, no extra blur pass,
  matches the toon aesthetic, the user's explicit choice). Shadow darkens the *existing* band
  ramp rather than painting a separate flat color: `CelShade` gained a `shadow` factor
  multiplied into `NdotL` before quantization, so a shadowed pixel just lands on a darker
  rung of the same ladder N.L already uses (also the user's explicit choice, after two rounds
  of ELI5, see the "shadow color" conversation if this needs revisiting later; the
  alternative, clamping straight to the ambient floor color regardless of band, is a one-line
  follow-up if the multiply-in look doesn't read well once tuned). Full technical write-up
  (abstraction-layer additions, the `iNumCascades = 0` sentinel, the two build gotchas: a
  cross-module `BasicStructures.fxh` namespace collision only caught at link time, and
  combined-sampler binding via the view rather than the SRB) now lives in MEMORY.md's
  "Cascaded shadow maps" section.
- **2026-07-13**: **Fixed-timestep sim loop shipped** (M1.1, first item of the M1 roadmap
  milestone added earlier the same day). Replaced `main.cpp`'s single variable-`dt` frame loop
  with an accumulator-driven fixed 60 Hz simulation tick, decoupled from the (still variable)
  render rate, with full "Fix Your Timestep!"-style render interpolation. Full mechanism,
  gotchas, and verification now in MEMORY.md's "Fixed timestep + render interpolation"
  section.
- **2026-07-13**: **Play / Pause / Step mode shipped** (M1.2, second item of the M1
  milestone; M1.3's entity behavior system is the one remaining item). Introduced an explicit
  `EditorMode { Editing, Playing, Paused }` on top of M1.1's fixed-timestep loop, with a
  Play-mode-isolation snapshot/restore convention and two temporal-interpolation bugs caught
  by tracing the math before writing code. Full design now in MEMORY.md's "Play / Pause /
  Step mode" section.
- **2026-07-13**: **Entity behavior system shipped** (M1.3, the last M1 item). Native
  scripts (`core/script.h`, Cherno/Hazel's `NativeScriptComponent` shape, EnTT deferred),
  `SpinScript` replacing the hardcoded spin block and the `spinners` side-list entirely, an
  explicit deep-cloning `Entity` copy constructor (the load-bearing consequence of
  `ScriptComponent` holding a `unique_ptr`), and `.scene` file persistence for scripts. See
  "Entity behavior system (roadmap M1.3)" in MEMORY.md for the full design, the mid-
  implementation deviation from the original plan (dropped a planned stream/file serializer
  split once the `Entity` copy ctor made it unnecessary and avoided a GPU-resource leak it
  would have caused), and the verification evidence (a real rotation delta matching the spin
  axis exactly, plus a temporarily-instrumented copy-ctor/save-load test). CLAUDE.md's
  roadmap pruning, its source-layout update, and `docs/architecture.md`'s corresponding
  update are deliberately left for a follow-up `tidy-md` pass, not done in this session.
- **2026-07-16**: **Physics + collision shipped** (M2.1). A quaternion `Transform.rotation`
  (replacing Euler, Phase A); a `core/physics.h`/`.cpp` abstraction layer twin to the
  renderer's, wrapping **Jolt Physics** behind an opaque `BodyHandle` + data-encapsulated
  `PhysicsWorld` (Phase B); independent
  `ColliderComponent`/`RigidBodyComponent` entity fields, Box/Sphere/Capsule x
  Static/Dynamic/Kinematic (Phase C); Play-time world construction and fixed-tick
  stepping/read-back, with a falling-primitives demo scene (Phase D); an Inspector "Physics"
  UI, reworked mid-phase after a direct correction into fully independent Light/Collider/
  Rigid Body/Scripts sections with real Add/Remove buttons, not the merged checkbox design
  first attempted (Phase E); and a collider debug wireframe overlay, which caught two real
  runtime bugs (an unbound pixel-shader constant, a forward-reference build error) plus one
  overlay-vs-physics accuracy bug during verification, all fixed before shipping (Phase F).
  See "Physics + collision (roadmap M2.1)" in MEMORY.md for the full design, the Phase E
  correction in the user's own words, and every bug's root cause; see "Build gotchas" in
  MEMORY.md for the three Jolt-specific CMake/compile gotchas hit along the way. Docs sync
  (this section, the History entry, CLAUDE.md, `docs/architecture.md`, README.md) folded
  into the same `tidy-md` pass rather than deferred, unlike M1.3's.
- **2026-07-16**: **`src/` reorganized**: `core/`'s 16 flat files split into subsystem
  folders (`core/rendering/`, `core/scene/`, `core/physics/`, `core/camera/`; `core/math.h`
  and `core/input/` stayed put), and `main.cpp`'s ~1600-line `main()` (which had accumulated
  the whole editor's init/tick/render/UI-panel logic) extracted into `src/app/` (an
  `EditorState` struct plus `editor_init/tick/render.{h,cpp}`, `physics_glue.{h,cpp}`, and
  `scene_ops.{h,cpp}` free functions) and `src/ui/panels/` (one file per ImGui panel, each a
  free function taking `EditorState&`). `main.cpp` is now ~90 lines of pure init/loop glue,
  with no Diligent header and no direct `ImGui::` calls. `EditorState` is a plain struct, not
  a class, for the same reason `Scene` is (see "Architecture decisions" -> "Data-oriented
  discipline" in MEMORY.md): nothing here hides a third-party dependency, so a class would have
  bought nothing but ceremony. Verified via a clean rebuild and a live-window screenshot
  (every panel plus the demo scene still rendering correctly). CLAUDE.md's Source layout and
  `docs/architecture.md` were updated to match in the same pass. Comment content in the
  moved/new files was carried over largely as-is; a separate later pass is intended to
  rewrite comments so they explain the code without leaning on this session's own history.
- **2026-07-20**: **Audio shipped (roadmap M2.2).** Full writeup under "Audio" in MEMORY.md.
  Headline points: `core/audio/audio.{h,cpp}` is a third PIMPL seam (`SoundHandle`,
  `AudioEngine`) twinning `Renderer`/`PhysicsWorld`, built on the new **miniaudio** submodule;
  a listener driven from the interpolated editor-camera pose each rendered frame (not the
  fixed sim tick, since audio is presentation, like rendering, not simulation); a new
  `AudioSource` entity component plus `BuildAudioWorld` glue mirroring `BuildPhysicsWorld`;
  and Playback/Properties/Settings panel UI (Play/Pause/Stop, per-source Preview, master
  volume/mute). Fixed a real bug along the way: `Entity`'s copy constructor's explicit
  member-init list had never been extended for `audioSource`, silently dropping the
  component on every scene copy. Docs synced in the same pass: this entry plus the "Audio"
  technical section, CLAUDE.md's roadmap (M2 item 1 removed), README's Highlights.
- **2026-07-20**: **Mouse-pick via raycast shipped** (M2.3, `docs/roadmap.md`'s former item
  8): geometric click-to-select (`app/picking.{h,cpp}`'s `PickEntity`/`DoMousePicking`),
  deliberately not wired to `PhysicsWorld::Raycast` (Jolt bodies only exist while Playing, and
  only collider-bearing entities would be hittable; editor selection needs both). New
  `Renderer::ScreenPointToRay`/`GetMeshBounds`/`GetModelBounds`; collider-less entities
  (lights, empty anchors) get a fixed-size pick box, visualized via a `DrawWireframe` marker.
  Verified by build, API-signature checks against the vendored ImGui/ImGuizmo/Diligent
  headers, and a temporary instrumented run (removed before commit) confirming the unproject
  math and nearest-hit resolution against live camera/scene state; no synthetic input reaches
  this environment's windows, so a live click couldn't be driven directly.
- **2026-07-20**: **Roadmap-skill reorg, user-directed.** `docs/roadmap.md`'s shipped-item
  promotion (verify against real code, migrate detail into MEMORY.md, move the item into
  "Shipped," renumber, recompute the progress line, recolor the mermaid diagram) moved from
  `tidy-md` to a new "Promote Anything That's Actually Shipped" step in `update-roadmap`; see
  that skill's own file for the full mechanic and its new shipped-node mermaid convention
  (rename the node's `N`-id to an `S`-id matching its Shipped position, move it into a shared
  `classDef shipped` reusing `v01`'s green, but leave it inside its own thematic milestone
  subgraph rather than relocating it, since that grouping is chronological, not a
  shipped/unshipped signal). `tidy-md`'s `docs/roadmap.md` section narrowed to prose/
  staleness checks only, the same treatment as `docs/architecture.md`; `plan-roadmap`'s two
  stale `tidy-md` cross-references were updated to `update-roadmap` in the same pass. Same
  session: a fresh `update-roadmap` triage pass added two Steam-release-gap items
  (Steamworks SDK bootstrap; controller-navigable UI + Steam Deck on-screen keyboard) and
  four wording amendments (frustum culling widened to name the main pass too, an
  instancing/PSO-batching note, a settings-menu borderless/per-device note, a
  crash-reporter third-party-SDK note), all confirmed via `AskUserQuestion` before writing.
  A following `tidy-md` pass fixed a pre-existing stray `#` typo in the roadmap mermaid's
  `N10` label (both `docs/roadmap.md` and README's duplicate copy), synced `docs/
  architecture.md`'s `Renderer` class listing and Source Layout to the mouse-pick additions
  above, and re-synced README's own copy of the roadmap mermaid diagram, which had drifted
  out of step with `docs/roadmap.md`'s structure.
- **2026-07-20**: **MEMORY.md reorganized, ARCHIVE.md created.** `MEMORY.md` (2900 lines)
  reorganized by topic instead of chronology, at the user's request. Four features that had
  only ever been written up inside this History log (not under any topic heading) got real
  sections: Cascaded shadow maps, Fixed timestep + render interpolation, Play / Pause / Step
  mode, and a new Temporal ghosting fixes section distilling the Rounds 1-7 saga above into
  its four durable root causes. This History section itself shrank to a compact one-line-
  per-entry pointer list in `MEMORY.md`; this file (`ARCHIVE.md`) was created to hold the
  full unedited version of this log, the full Rounds 1-7 narrative, and the original
  ToonEngineOld carry-over survey, none of which need to stay in the everyday lookup path.
