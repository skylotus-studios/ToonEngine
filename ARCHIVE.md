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
