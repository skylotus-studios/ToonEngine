# ToonEngine Architecture

This document describes how ToonEngine's pieces fit together: the renderer's abstraction
layer, the frame loop, the rendering pipeline, the scene model, and how data flows between
them. It's the deep reference for onboarding onto the codebase. For the project's guiding
principles, conventions,
and roadmap, see [CLAUDE.md](../CLAUDE.md); for the history and reasoning behind individual
decisions (and a long list of hard-won build/API gotchas), see [MEMORY.md](../MEMORY.md).

The app today is an editor: it opens a window, builds a small demo scene, and renders it live
through a full toon rendering pipeline — cel-shaded fill with inverted-hull outlines, cascaded
shadow maps, an HDR G-buffer, and a DiligentFX post-processing chain — with a docked Dear ImGui
UI for inspecting and editing the scene. The defining architectural decision, covered first
below, is the renderer's abstraction layer: every Diligent type and header is quarantined
inside one translation unit, so the rest of the engine — the scene graph, the editor camera,
the input system, the serializer, the UI panels, and gameplay systems (native scripts,
physics, audio next) — never touches Diligent directly and stays backend-agnostic by
construction. Physics (M2.1) added a second, twin abstraction layer the same way: every Jolt
Physics type is quarantined inside `core/physics.cpp` (see "The physics abstraction layer"
below).

## The renderer abstraction layer

`core/renderer.h` is the one header the rest of the engine includes for GPU work. Its file
banner states the contract directly: no Diligent type, and no Diligent header, escapes it. It
includes only `<cstdint>` and `core/math.h`, and forward-declares `struct GLFWwindow`, so
including it pulls in neither GLFW nor Diligent. `core/renderer.cpp` is the only translation
unit allowed to include a Diligent header or name a `Diligent::` type.

### Vocabulary: opaque handles and plain scene types

GPU resources are named by plain, scoped 32-bit enums, never by the underlying Diligent object:

```cpp
enum class TextureHandle : uint32_t { Invalid = 0 };
enum class BufferHandle : uint32_t { Invalid = 0 };
enum class ShaderHandle : uint32_t { Invalid = 0 };
enum class PipelineHandle : uint32_t { Invalid = 0 };
enum class MeshHandle : uint32_t { Invalid = 0 };
enum class ModelHandle : uint32_t { Invalid = 0 }; // a loaded glTF/GLB asset
```

`BufferHandle`, `ShaderHandle`, and `PipelineHandle` exist for contract completeness — their
creation APIs land with future shader/pipeline work — but only `MeshHandle`, `ModelHandle`, and
`TextureHandle` are live today. The id-to-resource mapping lives entirely inside the renderer's
`Impl`; nothing outside `renderer.cpp` ever sees the real object behind a handle.

Alongside the handles, `renderer.h` defines the plain scene vocabulary the rest of the engine
speaks in:

- **`Vertex`** — `position`, `normal`, `smoothNormal` (three tightly-packed `Vec3`s). `normal`
  shades the fill and may be faceted per-face; `smoothNormal` is the averaged normal the
  inverted-hull outline extrudes along, so hard edges (a cube's corners) stay closed instead of
  splitting apart. The two are identical on a smooth mesh.
- **`Camera`** — an orbit camera (`pivot`, `distance`, `yaw`, `pitch`, `fovY`, `nearZ`, `farZ`)
  plus editor-control tuning the renderer itself never reads (`lookSensitivity`,
  `panSensitivity`, `zoomSpeed`, `moveSpeed` — consumed only by `core/camera.h`). The renderer
  builds the actual view/projection matrices from these fields; keeping NDC and handedness
  conventions on the Diligent side of the abstraction layer means the app never has to know
  them.
- **`Material`** — the per-object toon look passed to `DrawMesh`/`DrawModel`: `baseColor`,
  `outlineColor`, `outlineWidth`, `bands` (shading band count), `ambient` (shadow-side floor),
  `roughness` (gates screen-space reflections).
- **`Transform`** — `position`, `rotation` (a `Quat`, replacing the old Euler-angle field so
  physics write-back and render interpolation need no lossy decompose/recompose each tick,
  and interpolation can slerp instead of a per-axis lerp; the inspector still edits Euler
  degrees, converted at the widget boundary), `scale` (may be non-uniform — `DrawMesh`
  derives an inverse-transpose normal matrix so shading, the normal G-buffer, and the outline
  width all stay correct under any scale).
- **`PostParams`** — the full live post-processing control block the Settings panel edits:
  exposure/tone-map, Bloom, SSAO, depth of field, TAA, SSR, and a `shadows` toggle (default
  `true`), each with its own parameters. It also carries `suppressTemporalHistory`, an
  app-computed flag (not a UI toggle) set whenever a gizmo drag, an edited ImGui widget, or
  continuous animation means temporal history shouldn't be trusted — see "The motion-history
  chain" below for why.

### The `Renderer` class

`Renderer` is a non-copyable, data-encapsulated class: every public method has a plain,
Diligent-free signature, and `struct Impl` is only forward-declared in the header. Its real
definition, and every
Diligent type it holds, lives in `renderer.cpp`.

```cpp
class Renderer {
public:
    bool Init(GLFWwindow* window);
    void Shutdown();
    void BeginFrame(const Color& clearColor);
    void EndFrame();
    void Resize(uint32_t width, uint32_t height);

    MeshHandle CreateMesh(const Vertex*, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount);
    void SetCamera(const Camera&);
    void SetLight(const Vec3& directionToLight, const Vec3& color, float intensity);
    void GetViewProj(Mat4& view, Mat4& proj) const;
    void DrawMesh(MeshHandle, const Transform&, const Transform& prevTransform, const Material&);
    void DrawMesh(MeshHandle, const Mat4& world, const Mat4& prevWorld, const Material&);

    uint32_t BeginShadowPass();
    void BeginShadowCascade(uint32_t cascadeIndex);
    void DrawMeshShadow(MeshHandle, const Mat4& world);
    void DrawModelShadow(ModelHandle, const Mat4& world);
    void EndShadowPass();

    ModelHandle LoadModel(const char* path);
    void DrawModel(ModelHandle, const Transform&, const Transform& prevTransform, const Material& style);
    void DrawModel(ModelHandle, const Mat4& world, const Mat4& prevWorld, const Material& style);

    TextureHandle LoadTexture(const char* path);
    void DestroyTexture(TextureHandle);
    uint64_t GetTextureImGuiID(TextureHandle) const;
    void GetTextureSize(TextureHandle, uint32_t& width, uint32_t& height) const;

    void SetPostParams(const PostParams&);
    void EndScene();

    void DrawWireframe(const Mat4& world, const Vec3* points, uint32_t count, const Color&);

    bool InitUI(GLFWwindow* window);
    void ShutdownUI();
    void BeginUI();
    void EndUI();

private:
    bool CreateToonPipeline();
    bool CreateModelPipeline();
    bool CreatePostPipeline();
    bool CreatePostFX();
    bool CreateOffscreenTargets(uint32_t width, uint32_t height);
    bool CreateShadowMap();

    struct Impl;
    Impl* m_impl = nullptr;
};
```

Grouped by what they do: lifecycle (`Init`/`Shutdown`/`BeginFrame`/`EndFrame`/`Resize`); the
toon draw path (`CreateMesh`, `SetCamera`, `SetLight`, `DrawMesh` — two overloads, one taking a
`Transform` pair and one a pre-composed `Mat4` pair for callers that already compose hierarchy
transforms); the cascaded-shadow pre-pass (`BeginShadowPass` through `EndShadowPass`); glTF
models (`LoadModel`, `DrawModel` — mirroring `DrawMesh`'s two-overload shape); editor-only
textures (`LoadTexture` etc., for the asset browser — not part of the toon draw path, since
materials don't carry textures yet); post-processing (`SetPostParams`, `EndScene`); the
collider debug overlay (`DrawWireframe`, M2.1 — see "The physics abstraction layer" below and
"The rendering pipeline"'s "Collider debug wireframe"); and the Dear ImGui glue (`InitUI` through
`EndUI`). "The rendering pipeline" below walks through what each group actually does on the
GPU.

### Why data encapsulation, not a virtual `IRenderer`

MEMORY.md's "Architecture decisions" section records the reasoning: Diligent already provides
runtime backend selection (Vulkan/D3D12/GL/Metal) beneath the abstraction layer, so a second
layer of runtime polymorphism in ToonEngine would buy nothing. A backend swap or console port
is a
build-time concern — write another `renderer_*.cpp` and point the build at it — not a rewrite,
and not a virtual-dispatch cost on the hot path.

### Dear ImGui is exempt

Dear ImGui is a plain UI library, not a Diligent type, so engine and game code may `#include
"imgui.h"` and call `ImGui::` directly — `main.cpp` does, extensively, between `BeginUI()` and
`EndUI()`. Only ImGui's Diligent render backend (`ImGuiImplDiligent`) is confined to
`renderer.cpp`.

## The physics abstraction layer

`core/physics.h` mirrors the renderer's abstraction layer almost exactly, for the same
reason: every Jolt Physics type and header is quarantined inside `core/physics.cpp`, so the
rest of the engine
never depends on the physics library directly. `physics.h` includes only `core/math.h` and
speaks purely in `toon::` types — no `JPH::` type escapes it.

```cpp
enum class BodyHandle : uint32_t { Invalid = 0 };
enum class ColliderShape { Box, Sphere, Capsule };
enum class BodyType { Static, Dynamic, Kinematic };

struct BodyDesc {
    ColliderShape shape;
    Vec3 extents;   // half-extents (Box); {radius,0,0} (Sphere); {halfHeight,radius,0} (Capsule)
    BodyType type;
    float mass, friction, restitution;
    Vec3 position;
    Quat rotation;
};

struct RaycastHit { BodyHandle body; Vec3 point; Vec3 normal; float distance; };
```

`PhysicsWorld` is data-encapsulated, the same shape as `Renderer` and for the same reason
(see "Why data encapsulation, not a virtual `IRenderer`" above — Jolt already *is* the one
physics implementation this engine uses, so a second layer of runtime polymorphism over it
would buy nothing):

```cpp
class PhysicsWorld {
public:
    bool Init();
    void Shutdown();
    void Clear();                                                // remove all bodies, keep the world alive
    BodyHandle CreateBody(const BodyDesc&);
    void DestroyBody(BodyHandle);
    void SetBodyTransform(BodyHandle, const Vec3&, const Quat&);  // teleport / kinematic push
    void GetBodyTransform(BodyHandle, Vec3&, Quat&) const;        // read back after Step
    void Step(float dt);
    void SetGravity(const Vec3&);
    bool Raycast(const Vec3& origin, const Vec3& dir, RaycastHit&) const;  // shipped, not wired to selection yet

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

std::vector<Vec3> ColliderWireframe(ColliderShape, const Vec3& extents);  // pure math, no Jolt
```

`Init` does Jolt's required one-time setup: a default allocator, a `Factory`,
`RegisterTypes`, a temp allocator, a job-system thread pool, and the three filter classes
Jolt's own `PhysicsSystem::Init` requires (`BroadPhaseLayerInterface`,
`ObjectVsBroadPhaseLayerFilter`, `ObjectLayerPairFilter`). See MEMORY.md's "Build gotchas"
for the CMake and compile gotchas hit getting this to build at all. `GetBodyTransform` reads
back through Jolt's `GetPositionAndRotation`, deliberately not `GetCenterOfMassPosition`
(which returns the body's center of mass, not its origin, and the two only coincide for a
shape whose mass is symmetric about that origin).

`ColliderWireframe` is the one function here with no Jolt dependency at all: given a shape
and its extents, it returns a flat list of line-segment endpoints (a box's 12 edges, a
sphere's 3 orthogonal great circles, a capsule's 2 rings + 4 struts + 4 hemisphere-cap arcs)
for the debug overlay described in "The rendering pipeline" below.

## Source layout

```
src/
  main.cpp                     Entry point: window + game loop; builds the scene, drives Renderer. No Diligent includes.
  icon.rc.in                   CMake-configured Win32 resource script; embeds GLFW_ICON for the taskbar/Alt-Tab icon.
  core/
    renderer.h                 The abstraction layer: opaque handles + scene types (Vertex/Camera/Material/Transform/PostParams) + data-encapsulated Renderer.
    renderer.cpp                Diligent (Vulkan) backend behind the abstraction layer: PSOs, MRT targets, cascaded shadow maps, DiligentFX post chain, ImGui glue.
    math.h                      Dependency-free Vec2/Vec3/Vec4 + a data-only row-major Mat4 — the abstraction layer's public-API vocabulary.
    scene.{h,cpp}                Entity-tree scene graph: hierarchy, world-transform composition, editor mutations.
    script.{h,cpp}               Native gameplay scripts: per-entity Update hooks + name->factory registry (M1.3).
    scripts/
      spin_script.{h,cpp}         First concrete Script: replaces main.cpp's old hardcoded spin block.
    physics.{h,cpp}              The physics abstraction layer: opaque BodyHandle + data-encapsulated PhysicsWorld (Jolt Physics), ColliderWireframe (M2.1).
    camera.{h,cpp}               Editor camera: orbit/pan/zoom/fly/focus, derived from the same Diligent matrices SetCamera uses.
    primitives.{h,cpp}          Procedural CPU mesh generators (sphere/cube/torus/plane) + PrimitiveDesc provenance for serialization.
    serializer.{h,cpp}          Scene save/load — entity/camera state to a text .scene file.
    input/
      keycodes.h                 Key/MouseButton/GamepadButton/GamepadAxis/MouseAxis enums (Key mirrors GLFW codes).
      input_device.h             Raw per-device state (Keyboard/Mouse/Gamepad) with current/previous snapshots for edge detection.
      input_system.{h,cpp}       GLFW callback wiring + the polling API, the ImGui capture gate, BeginFrame poll.
      action_map.{h,cpp}         Named rebindable actions/axes (FNV-1a hashed), a push/pop context stack.
      binding_io.{h,cpp}         JSON load/save of an InputContext's bindings (assets/input.json).
  ui/
    file_browser.{h,cpp}        "Asset Browser" panel: breadcrumb nav, sortable file table, preview pane.
    thumbnail_cache.{h,cpp}      Path -> TextureHandle cache for the browser's inline icons/preview (remembers failures too).
```

CMake bakes several absolute path macros into the binary (`target_compile_definitions` in
`CMakeLists.txt`), so the app finds its assets regardless of the working directory:

| Macro | Points at |
|---|---|
| `TOON_SHADERS_DIR` | `assets/shaders` — the HLSL shader source root |
| `TOON_MODELS_DIR` | `assets/models` — glTF/GLB/FBX test models |
| `TOON_FONTS_DIR` | `assets/fonts` — editor UI fonts |
| `TOON_ICON_PATH` | `assets/icon.png` — runtime window icon |
| `TOON_SCENES_DIR` | `assets/scenes` — saved `.scene` files |
| `TOON_INPUT_JSON` | `assets/input.json` — saved input bindings |
| `TOON_ASSETS_DIR` | `assets` — the asset browser's root |

A shipped build would copy these directories next to the executable and switch to relative
paths; today they're dev-convenience absolute paths into the source tree.

`assets/shaders/` holds the HLSL, cross-compiled to SPIR-V by Diligent at runtime:

| Shader | Role |
|---|---|
| `toon_common.hlsli` | Shared `Constants` cbuffer, `VSInput`/`PSInput`/`PSOutput`, `ComputeMotion`, `CelShade`, and the cascaded-shadow `ShadowAttribsCB`/`ComputeShadowFactor` (`#include`s DiligentFX's `Shadows.fxh`/`BasicStructures.fxh`). |
| `toon_fill.hlsl` | Procedural-mesh fill: banded diffuse × light color × shadow factor; writes color, world normal (+ roughness in `.w`), motion. |
| `toon_outline.hlsl` | Inverted-hull outline: extrudes along the smooth normal by a world-space-constant width. |
| `model_fill.hlsl` | glTF cel fill: the same ramp + shadow factor over a pos/normal/UV vertex, textured with `Texture2DArray g_Albedo`. |
| `model_outline.hlsl` | Inverted-hull outline for glTF models, extruded along the shading normal (models carry no smooth normal). |
| `shadow_depth.hlsl` | Depth-only pass for procedural meshes into a shadow cascade. |
| `model_shadow_depth.hlsl` | Depth-only pass for glTF models into a shadow cascade. |
| `tonemap.hlsl` | Full-screen HDR resolve: AO/SSR composite, exposure, ACES tone map, optional sRGB encode. No vertex buffer — the triangle is generated from `SV_VertexID`. |
| `wireframe.hlsl` | Collider debug overlay (M2.1): flat-color line-list draw for `DrawWireframe`, no lighting/shadow/G-buffer output. |

## The frame loop

`main.cpp` includes `core/renderer.h`, the engine headers, plain `<GLFW/glfw3.h>`
(`GLFW_INCLUDE_NONE` is set engine-wide), and `imgui.h`/`imgui_internal.h`/`ImGuizmo.h` — no
Diligent header, matching the abstraction layer's contract. ImGui and ImGuizmo are exempt
from it (see above).

### Startup

Roughly: create the GLFW window with the `GLFW_NO_API` hint (Vulkan owns the surface, not GL);
`Renderer::Init`; `Input::Init` **before** `Renderer::InitUI` (so ImGui's GLFW backend chains
the app's own callbacks instead of overwriting them); register default editor input bindings
and load/create `assets/input.json`; `InitUI` (enables ImGui docking, loads the Bai Jamjuree
font, applies one of three built-in themes); wire the framebuffer-resize callback to
`Renderer::Resize`; `physicsWorld.Init()` (Jolt's `Factory`/`RegisterTypes` are process-global
one-time setup, see "The physics abstraction layer" above); build the demo scene graph (ground with a
static box collider, sphere, cube, satellite orbiting the cube, torus, a loaded glTF helmet, a
light entity, and a few dynamic Box/Sphere colliders dropped above the ground — the
sphere/cube/torus/helmet each get a `SpinScript` attached, see "The scene model" -> "Scripts"
below; the dynamic primitives get a `ColliderComponent` + `RigidBodyComponent` instead, see
"The scene model" -> "Physics components" below); construct the editor `Camera`, the shared
`Material style`, `PostParams post`, and the `FileBrowser`.

### Per frame

```
Input::BeginFrame()              // snapshot prev state, clear deltas -- BEFORE glfwPollEvents
glfwPollEvents()                 // callbacks accumulate into the freshly-cleared frame
frameTime = now - lastTime, clamped to <= 0.25s   // spiral-of-death guard (e.g. a window drag stalls glfwPollEvents)
dt = frameTime                                     // frame-rate concerns below (camera nav) use this

// Fixed-timestep sim, gated by EditorMode (see "The editor layer" -> "Play / Pause / Step"):
// only Playing feeds the accumulator from frameTime -- Editing/Paused freeze it. A Step
// request credits it with exactly one kFixedDt instead, so the loop below drains one tick.
if (mode == Playing) accumulator += frameTime
if (stepRequested)   accumulator += kFixedDt
if (mode == Playing || stepRequested):
    while (accumulator >= kFixedDt):
        SnapshotSimState(scene)          // prevSimTransform = transform, for every transformed entity
        if (runScripts) UpdateScripts(scene, kFixedDt)   // every entity's attached scripts' OnUpdate (core/script.h)
        // Physics (M2.1, "The physics abstraction layer" above): static/kinematic bodies follow their
        // entity transform; Step advances the simulation; dynamic bodies are read back.
        for entity with a static/kinematic RigidBodyComponent: physicsWorld.SetBodyTransform(handle, transform)
        physicsWorld.Step(kFixedDt)
        for entity with a dynamic RigidBodyComponent: transform = physicsWorld.GetBodyTransform(handle)
        accumulator -= kFixedDt

// Outside Playing, alpha is pinned to 1.0: the accumulator isn't draining, so any leftover
// fraction from a previous Play session is stale and must not blend a paused/edited pose against it.
alpha = (mode == Playing) ? accumulator / kFixedDt : 1.0
UpdateWorldTransforms(scene, alpha)  // snapshots prevWorldMatrix, then composes local * parentWorld
                                      // from lerp(prevSimTransform, transform, alpha) per entity

// Editor camera: gate on ImGui capture OR an active gizmo drag, then navigate
Input::SetCaptured(io.WantCaptureMouse || gizmoActive, io.WantCaptureKeyboard)
suppressTemporalHistory = gizmoActive || ImGui::IsAnyItemActive() || runScripts || suppressNextFrameHistory
  right-drag  -> CameraOrbit (+ WASD/QE fly via the "camera.fly.*" action map)
  middle-drag -> CameraPan
  scroll      -> CameraZoom
  F           -> CameraFocus
  gamepad right stick -> CameraOrbit

renderer.SetPostParams(post)     // BEFORE SetCamera: SetCamera reads post.taa for the jitter decision
renderer.SetCamera(camera)
renderer.SetLight(lightDir, lightColor, lightIntensity)   // from GetActiveLight(scene), or a fixed fallback

// Cascaded-shadow pre-pass: its own depth-only targets, so it must run before BeginFrame
// binds the main G-buffer. BeginShadowPass returns 0 (loop below is a no-op) if the Debug
// panel's Shadows toggle is off.
shadowCascades = renderer.BeginShadowPass()
for cascade in 0..shadowCascades:
    renderer.BeginShadowCascade(cascade)
    for entity in scene.entities: renderer.DrawMeshShadow / DrawModelShadow(entity.worldMatrix)
renderer.EndShadowPass()

renderer.BeginFrame(clearColor)  // binds + clears the HDR/normal/motion MRT + depth

for entity in scene.entities:    // skip non-renderable (root / light) entities
    material = entity.material; overlay style.bands / style.ambient / outlineScale
    renderer.DrawMesh(entity.mesh, entity.worldMatrix, entity.prevWorldMatrix, material)
    // or renderer.DrawModel(...) for a glTF entity

renderer.EndScene()              // PostFX chain + exposure + tone map -> back buffer

renderer.BeginUI()               // ImGui::NewFrame
ImGuizmo::BeginFrame()
  gizmo hotkeys (W/E/R/X),
  a main menu bar (File/Edit/Tools/View/Help),
  dockspace,
  Playback panel (Play/Pause toggle, Step, Stop, a Mode status label -- see "The editor
    layer" -> "Play / Pause / Step" below),
  Objects panel (select / add-child / duplicate / delete / drag-drop reparent
    -- records structural ops mid-iteration, applies them AFTER the loop, since a
    mutation reorders the entity vector and invalidates in-flight indices),
  Properties panel (name / transform / material, always present; light, collider, rigid
    body, and scripts each independently added/removed / ImGuizmo Manipulate),
  Settings panel (band count, style, camera tuning, Show Colliders, every PostParams
    toggle -- theme is a View-menu command now, not a Settings-panel control),
  assetBrowser.Render(renderer) -> a double-clicked .scene routes through the same
    load path as the File menu's "Open Scene..." command
renderer.EndUI()                 // ImGui renders onto the still-bound back buffer

renderer.EndFrame()               // Present
```

Teardown runs the mirror image: `Input::Shutdown()`, `assetBrowser.Shutdown(renderer)` (frees
thumbnail textures before the device goes away), `renderer.Shutdown()`, destroy the window,
`glfwTerminate()`.

Four ordering rules are load-bearing and easy to get wrong if this code moves: `Input::BeginFrame`
must precede `glfwPollEvents` (edge detection depends on it); `SetPostParams` must precede
`SetCamera` (the TAA jitter decision reads `post.taa`); the shadow pre-pass must run after
`SetCamera`/`SetLight` but before `BeginFrame` (it needs the current camera and light, and it
renders into its own targets that don't interact with the main G-buffer `BeginFrame` binds); and
in the fixed-step loop, `SnapshotSimState` must run before that iteration mutates any entity's
`transform` (it's what `UpdateWorldTransforms`'s `alpha` interpolates *from*), while `alpha`
itself must be computed *after* the `while` has fully drained the accumulator, not before, since
it reads the accumulator's final leftover for this frame.

## The rendering pipeline

End to end: **shadow pre-pass** (its own depth-only cascade atlas) → **main scene pass** into an
**HDR + world-normal + motion-vector G-buffer** (reading the shadow atlas inline) → **DiligentFX
post-FX** (SSAO and SSR produced separately; TAA → DoF → Bloom form a color chain) → **tone-map
resolve** (composites AO/SSR, applies exposure + ACES, writes the back buffer) → **ImGui overlay**.

### Internal formats and the shared cbuffer contract

```cpp
static constexpr TEXTURE_FORMAT kHDRFormat        = TEX_FORMAT_RGBA16_FLOAT; // scene color
static constexpr TEXTURE_FORMAT kNormalFormat     = TEX_FORMAT_RGBA16_FLOAT; // world normal (xyz) + roughness (w)
static constexpr TEXTURE_FORMAT kMotionFormat     = TEX_FORMAT_RG16_FLOAT;   // NDC motion (SSAO temporal / DoF)
static constexpr TEXTURE_FORMAT kSceneDepthFormat = TEX_FORMAT_D32_FLOAT;

static constexpr Uint32 kShadowCascades   = 4;
static constexpr Uint32 kShadowResolution = 2048;
```

`RGBA16_FLOAT` holds signed normals in `[-1,1]` directly, with no encode/decode step. The
normal buffer's `.w` doubles as the SSR roughness input, so no fourth MRT target is needed.

The toon and model fill/outline shaders share one dynamic constant buffer, and its C++ mirror
must match `toon_common.hlsli`'s `Constants` cbuffer field-for-field:

```cpp
// GPU mirror of the toon_common.hlsli cbuffer: five row-major float4x4 rows +
// five float4 rows = 400 bytes, 16-aligned.
struct ShaderConstants {
    float4x4 worldViewProj;
    float4x4 world;
    float4x4 normalMatrix;      // inverse-transpose of world (correct normals under non-uniform scale)
    float4x4 prevWorldViewProj; // previous frame, for motion vectors
    float4x4 prevNormalMatrix;  // inverse-transpose of the PREVIOUS frame's world (outline motion)
    float4 lightDir;
    float4 lightColor; // rgb = light color * intensity, premultiplied
    float4 baseColor;
    float4 outline;    // rgb color, w = extrude width
    float4 params;     // x = bands, y = ambient, z = roughness
};
```

Matrices are declared `row_major` in HLSL so Diligent's row-major `float4x4` uploads verbatim
(no transpose); shaders use row-vector `mul(v, M)` throughout. A size mismatch between this
struct and the `.hlsli` cbuffer trips an immediate Diligent validation error, which is how the
layout stays honest across edits.

### Shadow map creation runs first, before either toon pipeline

`Renderer::Init` calls its setup helpers in a specific order: `CreateShadowMap()` **before**
`CreateToonPipeline()`/`CreateModelPipeline()`, then `CreateOffscreenTargets`,
`CreatePostPipeline`, `CreatePostFX`. The reason is a real Diligent constraint, not a stylistic
choice: `CreateShadowMap` allocates the shadow atlas and binds it into the fill PSOs'
`ShadowAttribsCB`/`g_ShadowMap`/`g_ShadowMap_sampler` as `STATIC` shader resource variables, and
a static variable must be set before its pipeline's shader resource binding (SRB) is created —
so the shadow map has to exist before the toon/model PSOs that reference it do.

`CreateShadowMap` builds a `Diligent::ShadowMapManager` (`external/DiligentFX/Components`,
`ShadowMapManager.hpp`) — the component that owns cascade distribution and the depth-only atlas
— with `SHADOW_MODE_PCF`, `kShadowCascades` cascades at `kShadowResolution` each, plus a
comparison sampler (`LESS`, for hardware PCF via `SampleCmp`) and two depth-only PSOs
(`shadowPSO` for procedural meshes, `modelShadowPSO` for glTF, built from `shadow_depth.hlsl` /
`model_shadow_depth.hlsl`). Unlike the window-sized offscreen targets, the shadow atlas doesn't
resize with the swap chain — it's fixed at `Init`.

### The shadow pre-pass

Per frame, `BeginShadowPass()` maps a `ShadowMapAttribs` constant buffer and calls
`ShadowMapManager::DistributeCascadeInfo` to compute each cascade's light-space transform from
the current camera and light, returning the cascade count (`kShadowCascades`, or `0` if
`PostParams::shadows` is off — the caller's loop becomes a no-op). For each cascade,
`BeginShadowCascade(i)` binds and clears that cascade's depth slice; `DrawMeshShadow`/
`DrawModelShadow` render every shadow-casting entity into it with the depth-only PSOs
(`GetCascadeTransform(i).WorldToLightProjSpace` supplies the light-space WVP — no motion
vectors, no material, just position). `EndShadowPass()` is a no-op today, kept for symmetry and
future filtering modes.

The main pass later reads the finished atlas through `toon_common.hlsli`'s
`ComputeShadowFactor(worldPos, cameraSpaceZ)`: it transforms `worldPos` into the shared
light-facing space (`ShadowMapAttribs.mWorldToLightView`), picks the correct cascade from
`cameraSpaceZ` (which is just `pin.CurrClip.w`, since clip-space W already is camera-space Z —
no separate view matrix needed in the cbuffer), and calls DiligentFX's `FilterShadowMap` for
3×3 PCF. The result feeds `CelShade`'s `shadow` parameter, which multiplies `N·L` **before**
quantizing into bands — so a shadowed pixel lands on a darker rung of the same band ladder
lighting already uses, rather than reading as a separate flat overlay color.

### The toon two-pass draw

Both `fillPSO` and `outlinePSO` output three render targets (color/normal/motion) plus depth,
with `FrontCounterClockwise = True` (primitives are authored CCW-front for the left-handed
projection). **Fill culls back faces; outline culls front faces**, keeping the outline pass's
enlarged inverted hull. `DrawMesh` computes `wvp`/`prevWvp`/`normalMatrix`/`prevNormalMatrix`
from the current and previous world matrices, maps the whole `ShaderConstants` block once, and
draws **outline first, then fill** — the fill's nearer depth overwrites the enlarged shell
everywhere except the silhouette rim. `DrawModel` follows the identical two-pass shape for glTF
primitives, with `FrontCounterClockwise = False` (glTF's right-handed winding flips under the
left-handed projection) and a `Texture2DArray` albedo sample (slice 0) in place of a flat base
color.

### The DiligentFX post-FX chain

`RunPostFX` runs once per frame from `EndScene`. It first calls `PostFXContext::Execute` with
the current **and previous** depth SRV, the motion-vector SRV, and current/previous camera
attributes, giving every downstream effect real reprojection history instead of treating the
current frame as its own previous frame (a deliberate fix for an earlier ghosting bug — see
MEMORY.md's "Bugs found dogfooding"). Then, if the PSOs are ready:

- **SSAO** (if enabled) reads depth + the normal G-buffer and produces a separate ambient
  occlusion SRV. `ResetAccumulation` is forced whenever `!ssaoTemporal` or
  `suppressTemporalHistory` is set.
- **SSR** (if enabled) reads color + depth + normal (roughness from `.w`, `RoughnessChannel =
  3`) + motion and produces a separate reflection-radiance SRV.
- The **color chain** runs scene → **TAA** → **DoF** → **Bloom**, in that order — each enabled
  stage reads the previous stage's output, so `colorOut` ends on the last one that ran. TAA
  runs first specifically so DoF and Bloom process the anti-aliased image, not the raw one.

SSAO and SSR are never part of the color chain; they're always composited separately in the
resolve, described next.

### The tone-map resolve

`EndScene` picks the processed color (or the raw HDR scene if nothing in the color chain ran),
the AO texture (or a 1×1 white default), and the SSR texture (or a 1×1 black default), binds the
back buffer, and draws a full-screen triangle with `tonemap.hlsl`:

```
hdr *= lerp(1, ao * ao, ssaoStrength)   // AO squared: a stylized punch, not physically restrained
hdr += ssr.rgb * ssr.a * ssrStrength    // a simplified additive composite, no PBR BRDF/env map
hdr *= exposure
color = toneMap ? ACESFilm(hdr) : saturate(hdr)
// optional LinearToSRGB if the back buffer isn't natively an sRGB format
```

`EndScene` leaves the back buffer bound afterward so the ImGui overlay draws directly on top,
and copies the current depth into `prevSceneDepth` for next frame's reprojection history.

### Collider debug wireframe (M2.1)

`DrawWireframe` (see "The physics abstraction layer" above for `ColliderWireframe`, the
geometry it draws) uses its own small PSO: `PRIMITIVE_TOPOLOGY_LINE_LIST`, depth test
**off**, back-buffer-only render target — the same "back buffer only" shape as the tonemap
PSO above, reused rather than reinvented. Depth test is off deliberately: an always-on-top overlay avoids adding a
depth-tested variant's G-buffer MRT-compatibility risk, and a debug aid benefiting from never
being occluded is a reasonable trade. `main.cpp`'s "Show Colliders" toggle (Settings panel)
calls it once per collider-bearing entity, directly after `EndScene()` and before `BeginUI()`
— it draws straight onto the already-bound back buffer from the tone-map resolve above, so no
extra render-target binding is needed in `DrawWireframe` itself.

### ImGui glue

`InitUI` constructs `ImGuiImplDiligent` (which creates the ImGui context) **before**
`ImGui_ImplGlfw_InitForVulkan` — the GLFW backend calls `ImGui::GetIO()`, which asserts if no
context exists yet. `ShutdownUI` tears down in the exact reverse order; getting either wrong
either fails an assert on startup or aborts the process on window close. The ImGui PSO is built
with `TEX_FORMAT_UNKNOWN` for its depth format, since the UI draws to the back buffer with no
depth attachment bound.

## The scene model

`core/scene.h` is a flat entity tree: parents always precede their children in the vector, and
index 0 is the implicit root, so a single forward pass composes every world transform.

```cpp
struct Entity {
    std::string name;
    int parent = 0;                                    // -1 marks the root (index 0 only)
    std::optional<Transform> transform = Transform{};   // nullopt = pure anchor/grouping node
    std::optional<Transform> prevSimTransform;          // previous fixed sim tick's pose, for render interpolation
    Mat4 worldMatrix;                                   // cached, composed by UpdateWorldTransforms
    Mat4 prevWorldMatrix;                               // last frame's world, for motion vectors
    MeshHandle  mesh  = MeshHandle::Invalid;             // a procedural primitive, or...
    ModelHandle model = ModelHandle::Invalid;            // ...a loaded glTF model
    Material material;
    PrimitiveDesc primitive;    // provenance for serialization: regenerate a procedural mesh on load
    std::string   modelPath;    // provenance for serialization: reload a glTF model on load
    std::optional<LightComponent> light;                // set -> this entity is a directional light
    std::vector<ScriptComponent> scripts;               // attached native scripts, see "Scripts" below
    std::optional<ColliderComponent> collider;          // set -> this entity has a collision shape
    std::optional<RigidBodyComponent> body;             // set -> physics owns this entity's transform each tick
};

struct Scene {
    std::vector<Entity> entities;
    int selected = -1;   // editor selection; -1 = none
};
```

A light entity is aimed by its **rotation** (Unity/Godot-style): its local +Z axis in world
space is the direction light travels, so rotating the entity — with the gizmo, for instance —
re-aims it. `GetActiveLight` reads that direction off the first light entity's cached world
matrix (row 2), falling back to a fixed default if the scene has none.

`UpdateWorldTransforms(scene, alpha)` runs once per rendered frame, after the fixed-step sim loop
(see "The frame loop") has run zero or more ticks: for each entity in order, it snapshots
`prevWorldMatrix = worldMatrix` first, then recomputes `worldMatrix = local * parentWorld`, where
`local` comes from `LocalFromTransform` fed a pose **interpolated** between `prevSimTransform` and
`transform` by `alpha` (row-vector convention; the parent's world is already known because parents
precede children). `alpha` is the fixed-timestep accumulator's leftover fraction into the next sim
tick while Playing; the default `alpha = 1.0` renders the current tick exactly, which the main
loop itself now passes explicitly whenever `EditorMode` isn't Playing (see "The editor layer" ->
"Play / Pause / Step" below), not only for callers entirely outside the loop (e.g. right after a
scene load). `prevSimTransform` starts `nullopt` and is populated by
`SnapshotSimState` at the top of each fixed step, so a fresh or just-loaded entity interpolates
`transform` with itself: no spurious motion. The gizmo write-back path runs the inverse:
`SetEntityWorldMatrix` folds out the parent's world (`world * parent⁻¹`) and decomposes the result
to a local TRS, writing `transform`, the authoritative pose the next fixed step's
`SnapshotSimState` will pick up.

The editor mutation API (`AddChildEntity`, `DeleteEntity`, `DuplicateEntity`, `ReparentEntity`,
`MoveEntityAsSibling`) all preserve the parents-before-children invariant and fix up `selected`,
and all mutate the entity vector — so `main.cpp`'s hierarchy panel records requested operations
while iterating and applies them only after the loop ends. `ReparentEntity` is world-preserving
(the entity doesn't visually jump) and refuses cycles via `IsAncestorOrSelf`. `DestroyScene`
just clears the vector: entities hold only handles, never GPU resources, so there's nothing
GPU-side to release here (see "Ownership" below).

### Scripts

`core/script.h` (M1.3) is where per-entity gameplay logic lives — a native-script layer in
Cherno/Hazel's `NativeScriptComponent` shape, backed by `Entity::scripts` rather than an
`entt` registry (ECS stays a deliberate later option, not built; see MEMORY.md's "Entity
behavior system" section for the full survey and reasoning). `Script` is a virtual base:

```cpp
class Script {
public:
    virtual void OnCreate(Entity& self, Scene& scene) {}
    virtual void OnUpdate(Entity& self, Scene& scene, float dt) {}
    virtual void OnDestroy(Entity& self, Scene& scene) {}   // declared, not wired -- no mid-Play spawn/destroy yet
    virtual void Save(std::ostream& out) const {}
    virtual void Load(std::istream& in) {}
};
```

`self`/`scene` are call-time parameters, never stored on the `Script` instance: storing
either would dangle the moment `scene.entities` reallocates. A `Script` holds no private
simulation state; anything persistent lives on the `Entity` (or a future component), so a
script is a pure function of `(entity data, dt)`. See MEMORY.md for why: a future
rollback-netcode-style fast snapshot only ever needs the data this way, never the script
objects. `ScriptComponent` pairs a stable name with the live instance
(`{ std::string name; std::unique_ptr<Script> instance; }`); `RegisterScript`/`CreateScript`
form a name -> factory registry so a name loaded from a `.scene` file, or an in-memory clone
(next paragraph), can reconstruct the right subclass. `CreateScripts(scene)` fires each
script's `OnCreate` once, in entity order, when a Play session begins; `UpdateScripts(scene,
dt)` dispatches every entity's `OnUpdate` each fixed tick (see "The frame loop" above).
`core/scripts/spin_script.{h,cpp}`'s `SpinScript` is the first concrete example, replacing
what used to be a hardcoded block in `main.cpp`.

The load-bearing consequence: `Entity`/`Scene` lost their implicit copy operations. A
`std::unique_ptr` inside `ScriptComponent` deletes `Entity`'s implicit copy constructor and
assignment (and therefore `Scene`'s) the moment `scripts` isn't empty. `Entity` now declares
an explicit copy constructor/assignment (`core/scene.cpp`) that copies every other field
normally and deep-clones `scripts` by calling `CreateScript(name)`, then round-trips that one
script's own `Save`/`Load` through an in-memory string stream. It never touches the
`Renderer`, so mesh/model handles copy as plain IDs with no GPU re-upload. Move stays
`= default` (cheap: moves the vector's buffer, never touches an individual
`ScriptComponent`), required explicitly once a custom copy constructor is declared, or
`std::move` call sites would silently fall back to the (now expensive) copy. This one change
is what keeps `DuplicateEntity` (above) and Play/Stop's `sceneBackup = scene` / `scene =
sceneBackup` (see "The editor layer" -> "Play / Pause / Step" below) compiling and correct
with no call-site changes: a copy is no longer free, but it's still just a copy from the
caller's side.

A script serializes as one `.scene` line, `script <Name> <field...>`, resolved through the
registry on load — the same shape `primitive <kind> <field...>` already uses.

### Physics components (M2.1)

`ColliderComponent` (shape + extents) and `RigidBodyComponent` (mass/friction/restitution/
type + a transient, never-serialized `BodyHandle`) are independent `std::optional` fields,
matching the grain `LightComponent`/`ScriptComponent` already established — not one merged
component. A collider alone is an implicit static collider (a wall/floor with no authored
body); collider **and** body is a dynamic/kinematic mover, the same split Unity's
`Collider`/`Rigidbody` and Godot's `CollisionShape`/`RigidBody` both use. See "The physics
abstraction layer" above for `BodyHandle`/`BodyDesc`/`PhysicsWorld` themselves.

`BuildPhysicsWorld(physicsWorld, scene)` (`main.cpp`) is pure derived state, rebuilt from
scratch every time Play (or Step-from-Editing) begins: `Clear()`s the world, then for each
entity with a `ColliderComponent`, synthesizes an implicit static `RigidBodyComponent` if
none was authored, and calls `CreateBody` seeded from the entity's current world pose. This
assumes every collider-bearing entity is root-parented — a collider on a nested entity is
seeded once at Play-start but never re-synced against a moving parent afterward, since Jolt
bodies simulate in world space and this step doesn't fold a parent chain in. Non-uniform
scale has no exact representation for every shape: `Box`'s three half-extents bake a
non-uniform scale in exactly, one axis at a time, but `Sphere`/`Capsule` (1-2 degrees of
freedom) approximate it with the largest relevant axis and log a one-time warning naming the
entity.

## The editor layer

Everything in this section is Diligent-free; it only ever reaches the GPU across the
abstraction layer, through a `Renderer&`.

### Editor camera

`core/camera.h` exposes free functions (`CameraOrbit`, `CameraPan`, `CameraZoom`, `CameraFly`,
`CameraFocus`) that mutate a `toon::Camera` in place. `camera.cpp` derives the camera's
world-space basis from `RotationX(-pitch) * RotationY(-yaw)` — the same Diligent matrices
`Renderer::SetCamera` builds its view matrix from — so the controls agree exactly with what the
renderer actually does with yaw/pitch, rather than a hand-guessed set of axis signs.

### Procedural primitives

`core/primitives.h` generates `MeshData` (a plain vertex + index array) for a sphere, cube,
torus, or ground plane, all wound counter-clockwise as seen from outside — matching the fill
pass's back-face culling and the outline pass's front-face culling. Each primitive also carries
a `PrimitiveDesc` (kind + generation parameters), which is what lets the serializer regenerate a
procedural mesh on load instead of needing a source file.

### Serialization

`core/serializer.h`'s `SaveScene`/`LoadScene` read and write a human-readable, line-based
`.scene` text file. `LoadScene` parses into a side buffer and only swaps it into the caller's
`Scene`/`Camera` on full success, so a malformed file leaves the caller untouched. A procedural
entity's mesh is rebuilt via `renderer.CreateMesh(MakePrimitiveMesh(desc))`; a model entity's is
reloaded via `renderer.LoadModel(modelPath)`; a script reconstructs via `CreateScript(name)`
(see "The scene model" -> "Scripts" above) — the serializer's only contact with the GPU is the
mesh/model path, always across the abstraction layer. Loading resets `scene.selected` to `-1`.

### The input system

Layered, GLFW-based, and Diligent-free:

- **`keycodes.h`** defines `Key`/`MouseButton`/`GamepadButton`/`GamepadAxis`/`MouseAxis` (`Key`
  mirrors GLFW's own codes), isolating the rest of the system from GLFW's enum values directly.
- **`input_device.h`** holds raw per-device state (`Keyboard`, `Mouse`, `Gamepad`) with
  current/previous snapshots for edge detection (`WasKeyPressed` etc.).
- **`input_system.{h,cpp}`** installs the GLFW callbacks (`Init`, called before `Renderer::InitUI`
  so ImGui's backend chains rather than overwrites them) and exposes the polling API.
  `BeginFrame` snapshots previous state and polls gamepads, and must run before
  `glfwPollEvents`. An ImGui-capture gate (`SetCaptured`, fed from `io.WantCaptureMouse/Keyboard`)
  neutralizes the gated queries (`IsKeyDown`, `MouseDelta`, …) while the UI has focus;
  `RawKeyboard`/`RawMouse`/`GetGamepad` bypass the gate entirely for the action-map layer below.
- **`action_map.{h,cpp}`** layers named, rebindable actions and axes on top: a `Binding` is a
  `std::variant` over five physical-input types; an `Action` evaluates to on/off, an `Axis`
  combines a positive/negative binding set into one `-1..1` value (so keyboard WASD and a
  gamepad stick can drive the same named axis transparently). Actions are hashed by name
  (compile-time FNV-1a) for lookup, and a push/pop **context stack** lets a future mode (play
  vs. edit, say) shadow the bindings below it — today only an `"editor"` context is ever pushed,
  registering `camera.fly.forward/right/up`, `camera.orbit.x/y`, and `camera.focus`.
- **`binding_io.{h,cpp}`** persists an `InputContext`'s bindings as JSON (`assets/input.json`,
  via the nlohmann/json library DiligentTools already vendors), using the same
  side-buffer-swap-on-success contract as the scene serializer.

### Asset browser

`ui/file_browser.h`'s `FileBrowser` draws the "Asset Browser" panel: a breadcrumb bar, a
sortable file table, and an image preview pane, rooted at `assets/` (`TOON_ASSETS_DIR`). It's
passive beyond navigation — the one active behavior is returning the path of a file the user
double-clicked this frame; it has no notion of what a `.scene` file means, so `main.cpp` decides
that. `ui/thumbnail_cache.h`'s `ThumbnailCache` decodes an image to a texture once per path via
`Renderer::LoadTexture`, and remembers failures too, so a bad file is only ever attempted once.

### Play / Pause / Step

`EditorMode` (`main.cpp`, anonymous namespace, alongside `Theme`) is `Editing`, `Playing`, or
`Paused`, driving the fixed-timestep gating described in "The frame loop" above. A "Playback"
panel (Play/Pause toggle, Step, Stop, a `Mode: EDITING/PLAYING/PAUSED` status label) docks as a
thin strip at the top of the main dockspace, one more `DockBuilderSplitNode` alongside the
existing panel splits, not a new positioning mechanism.

Pressing Play snapshots `scene` into a `sceneBackup` (a `Scene` copy: a `vector<Entity>` plus
an `int`; mesh/model copy as plain handles, never touching the `Renderer` — see "The scene
model" -> "Scripts" above for why this copy is an explicit `Entity` constructor rather than a
free memberwise one now that entities can carry scripts) and switches to Playing, also firing
each script's `OnCreate` once (`CreateScripts`) and building the physics world from the
scene's collider-bearing entities (`BuildPhysicsWorld`, see "The scene model" -> "Physics
components" above). Pressing Stop always restores `scene = sceneBackup`, clears the physics
world (`PhysicsWorld::Clear`), and switches back to Editing, discarding whatever happened
during Play, the same disposable-sandbox convention Unity, Godot, and Unreal all use. This
was deliberately built as the safety net the entity behavior system (M1.3), and physics
(M2.1) after it, both rely on: testing gameplay should never risk permanently scrambling a
hand-placed scene. Step credits the accumulator with exactly one `kFixedDt` (starting a Play
session first, landing in Paused, if pressed from Editing) so the existing fixed-step `while`
loop drains exactly one tick, with no separate single-step code path.

A Stop-restore or a Step both cause a one-frame pose jump rather than smooth motion (a spun
transform snapping back on Stop; a whole tick advancing at once with no interpolation smoothing
it in on Step), so both set `suppressNextFrameHistory`, folded into `suppressTemporalHistory`
the next frame alongside the existing gizmo-drag/edited-widget/`runScripts` cases (see "The
motion-history chain" below).

## Data flow and ownership

### Ownership

| What | Owned by | Handle scheme |
|---|---|---|
| Meshes | `Renderer::Impl::meshes` (`vector<GpuMesh>`) | 1-based (`MeshHandle N` → `meshes[N-1]`); freed at `Shutdown` |
| glTF models | `Renderer::Impl::models` (`vector<unique_ptr<GLTF::Model>>`) | 1-based; each `GLTF::Model` owns its own GPU buffers + textures |
| Editor textures | `Renderer::Impl::textures` (`vector<RefCntAutoPtr<ITexture>>`) | 1-based; `DestroyTexture` nulls the slot (no compaction, handles stay stable) |
| Entities | `Scene::entities`, owned by the app (`main.cpp`) | Entities hold only handles + a `Material` + cached matrices, never GPU resources |

Because entities never hold GPU resources directly, `DestroyScene` is just `entities.clear()` —
the actual meshes/models/textures belong to the `Renderer` and are released at its own
`Shutdown`.

Materials are per-entity (`Entity::material`), but each frame `main.cpp` overlays the shared
`style` (`bands`, `ambient`) and `outlineWidth * outlineScale` onto a **per-draw copy**, so
scene-wide look controls and per-object color/outline coexist without the entity's own stored
material being overwritten. The single directional light is separate scene state, sourced from
`GetActiveLight` each frame rather than carried per-draw.

### Entity to draw call

The draw loop walks `scene.entities` in vector order, skips entities with neither a mesh nor a
model handle (the root, and light entities — nothing draws a light), and always uses the
pre-composed-`Mat4` overloads of `DrawMesh`/`DrawModel`, since the scene graph has already
composed hierarchy world transforms in `UpdateWorldTransforms`. The shadow pre-pass walks the
same entities but calls `DrawMeshShadow`/`DrawModelShadow` with only the current `worldMatrix` —
shadows don't need motion vectors.

### The motion-history chain

Every temporal effect (SSAO's accumulation, DoF, TAA, SSR) depends on per-object and per-camera
motion vectors being correct, which means threading "last frame's" state through several places
in the same order every frame:

1. `UpdateWorldTransforms` snapshots `entity.prevWorldMatrix = entity.worldMatrix` **before**
   recomputing this frame's `worldMatrix`, which is now composed from each entity's pose
   **interpolated** between its previous and current fixed sim tick, not the raw current tick
   (see "The scene model" above). Because both the previous and current `worldMatrix` are built
   the same interpolated way, `prevWorldMatrix` automatically means "last **rendered** frame's
   (interpolated) world": the fixed/render rate mismatch needs no separate bookkeeping here.
2. The draw loop passes both matrices into `DrawMesh`/`DrawModel`.
3. `SetCamera` snapshots `prevViewProj = viewProj` **before** overwriting it, so camera motion
   (orbit/zoom/pan/fly) is captured the same way object motion is.
4. `DrawMesh` computes `wvp = world * viewProj` and `prevWvp = prevWorld * prevViewProj`, plus
   `normalMatrix` and `prevNormalMatrix` (both inverse-transposes), and uploads all of them in
   `ShaderConstants`.
5. The vertex shader emits both `CurrClip` and `PrevClip`; the outline VS additionally
   re-extrudes the hull using `g_PrevNormalMatrix` rather than reusing this frame's, since the
   extrusion direction itself is rotation-dependent and reusing it would under-report motion
   during rotation. The pixel shader writes `Motion = ComputeMotion(CurrClip, PrevClip) =
   currNDC - prevNDC` into the RG16F motion target.
6. `main.cpp` sets `PostParams::suppressTemporalHistory` whenever a gizmo is active, an ImGui
   item is being edited, the Run Scripts toggle is on, or a Stop-restore/Step just happened
   (see "The editor layer" -> "Play / Pause / Step" above -- both are a one-frame pose jump,
   not smooth motion). `RunPostFX` then forces
   `ResetAccumulation` on SSAO and TAA — the fix for a documented ghosting bug where a slowly
   spinning silhouette's view-dependent contour slipped under DiligentFX's own compiled-in
   motion-distrust threshold (see MEMORY.md's "Bugs found dogfooding" for the full
   investigation).

## Build and dependencies

Dependencies are git submodules under `external/`, not vcpkg (see `.gitmodules`):
`DiligentCore`, `DiligentTools`, `DiligentFX`, `glfw`, `ImGuizmo`, `JoltPhysics`, and
ToonEngine's own `imgui` (pinned to upstream's `docking` branch — see below).

Jolt's own `CMakeLists.txt` lives under `Build/`, so it's added as
`add_subdirectory(external/JoltPhysics/Build)`, linking the `Jolt` target — with
`USE_STATIC_MSVC_RUNTIME_LIBRARY OFF` and `INTERPROCEDURAL_OPTIMIZATION OFF` set as `CACHE
BOOL ... FORCE` beforehand, the same cache-variable-before-`add_subdirectory` pattern the
`DILIGENT_NO_*` block below uses (Jolt otherwise defaults to a runtime library and an LTO
setting that both mismatch the rest of the build; see MEMORY.md's "Build gotchas" for the
exact link errors this produces).

`CMakeLists.txt` disables the Diligent backends and modules ToonEngine doesn't use, as `CACHE
BOOL ... FORCE` **before** `add_subdirectory(external/DiligentCore)` (these are cache variables,
so changing them needs a build-dir reconfigure to take effect): `DILIGENT_NO_DIRECT3D11`,
`DILIGENT_NO_DIRECT3D12`, `DILIGENT_NO_OPENGL`, and `DILIGENT_NO_RADIENT` (DiligentFX's
real-time GI module — unused, and it fails to compile under clang-cl).

Dear ImGui is built against ToonEngine's own `external/imgui` submodule rather than
DiligentTools' vendored one, by setting `DILIGENT_DEAR_IMGUI_PATH` before
`add_subdirectory(external/DiligentTools)` — DiligentTools only defaults that path if the
caller hasn't already set it. This is what makes `IMGUI_HAS_DOCK` (and the editor's dock space)
available at all; DiligentTools' own vendored imgui is not on the docking branch. `imgui_impl_glfw.cpp`
is compiled directly as a ToonEngine source, since DiligentTools' Imgui module only
auto-compiles the Win32/Linux-native/SDL/macOS platform backends, not GLFW.

Linked Diligent libraries and what each is for:

| Target | Used for |
|---|---|
| `Diligent-BuildSettings` | Platform defines (`PLATFORM_WIN32` etc.) and compiler flags; linked first |
| `Diligent-GraphicsEngineVk-shared` | The Vulkan backend, loaded at runtime (`ENGINE_DLL=1`, `LoadGraphicsEngineVk()`) |
| `Diligent-Common` | `RefCntAutoPtr.hpp` and other `Common/interface` utilities |
| `Diligent-GraphicsTools` | `MapHelper.hpp` (dynamic constant-buffer updates) and `IRenderStateCache` (unused today — the roadmap's planned shader-hot-reload path) |
| `Diligent-AssetLoader` | `GLTF::Model` — glTF/GLB loading, self-contained, no PBR renderer |
| `Diligent-TextureLoader` | `CreateTextureFromFile` — image decode for model textures and the asset browser |
| `Diligent-Imgui` | `ImGuiImplDiligent` render backend + the vendored Dear ImGui build |
| `Diligent-JSON` | nlohmann/json, for `binding_io.cpp`'s input-bindings persistence (linked explicitly — `Diligent-AssetLoader` pulls it in `PRIVATE`, so it doesn't propagate transitively) |
| `DiligentFX` | `PostFXContext`, Bloom, SSAO, DoF, TAA, SSR, and `ShadowMapManager` |
| `glfw` | Windowing + raw input |

`core/renderer.cpp` also includes two of DiligentFX's C++-side shader-structure headers
(`BasicStructures.fxh` → `CameraAttribs`, `BloomStructures.fxh` → `BloomAttribs`) directly,
inside `namespace Diligent::HLSL`, to build the host-side structs `PostFXContext`/`Bloom`
expect — this is why `CMakeLists.txt` adds `external/DiligentFX/Shaders/Common/public` to the
include path (`BasicStructures.fxh` does a bare `#include "ShaderDefinitions.fxh"`).

## Where new systems plug in

The cascaded shadow maps described above are the most recent example of the pattern any new
rendering feature follows: declare it in the abstraction layer, `renderer.h` (new methods, new
`PostParams` fields), implement it in `renderer.cpp` (new PSOs, new offscreen targets if
needed, wired into `Init`'s setup sequence), and add whatever new HLSL it needs under
`assets/shaders/`. The same shape applies to the roadmap's remaining M3 items: grid + sky
gradient is a new full-screen shader pass (like the tone-map resolve); 2D/sprites need a new
vertex format and a blended draw path; skeletal animation needs a bone/skinning vertex format
and per-frame joint-matrix upload, alongside a new animation entity component in `core/scene.h`.

Gameplay systems (the roadmap's M1 and M2.1, now shipped, and M2's remaining audio item) are
a different shape: they're Diligent-free by default, since nothing about a fixed-timestep
loop, an entity-behavior/update layer, physics, or audio needs to touch the renderer
directly. The first of M1, the fixed-timestep sim loop (see "The frame loop" above), shipped
as a restructuring of `main.cpp`'s existing loop plus two new `core/scene.h` members
(`prevSimTransform`, `SnapshotSimState`), not a new `core/` module: a scoped choice for that
item, not a pattern the next one had to follow. The second, the entity behavior system (M1.3,
see "The scene model" -> "Scripts" above), landed close to what this section used to predict:
an `Update`-style hook (`Script::OnUpdate`) called inside the fixed-step loop, before
`UpdateWorldTransforms`, right where the demo's spin used to advance inline, inheriting the
`EditorMode` gate for free. It did *not* stay a two-member addition, though. An open, growing
subsystem, and the landing spot for a future Lua bridge, got its own module (`core/script.h`
plus `core/scripts/`) rather than folding into `scene.cpp`.

The third, physics + collision (M2.1, see "The physics abstraction layer" and "The scene
model" -> "Physics components" above), also got its own module and landed close to this
section's own
prediction, with one refinement the prediction didn't anticipate: rather than physics owning
one merged component, `ColliderComponent` and `RigidBodyComponent` are independent optional
fields, matching Unity/Godot's own Collider/Rigidbody split. The fixed-loop half of the
prediction held exactly: physics steps inside the same `kFixedDt` loop scripts already use,
writing dynamic bodies' results back into `entity.transform` each tick, the same landing spot
a script's write already used. The predicted `core/sim_clock` extraction never became
necessary — the accumulator/`kFixedDt` loop still didn't change shape, only gained one more
thing to run inside it, the same outcome M1.3 had. Audio (M2's other item) is expected to need
no fixed-loop involvement at all, unlike physics and scripts: sound playback has no
per-tick simulation state to step, just fire-and-forget or streaming voices triggered by
scripts/gameplay events.

See MEMORY.md's "ToonEngineOld carry-over" section (and its "Port gotchas for the un-shipped
systems" subsection) for the concrete algorithms and gotchas behind the still-open M3 items.

## See also

- [CLAUDE.md](../CLAUDE.md) — guiding principles, conventions, build instructions, roadmap.
- [MEMORY.md](../MEMORY.md) — the detailed history and reasoning behind every decision here,
  plus a long list of build and API gotchas.
- [docs/cpp-style-guide.md](cpp-style-guide.md) — C++ house style and abstraction-layer rules.
- [docs/md-style-guide.md](md-style-guide.md) — the prose style this document follows.
- [docs/clion-setup-windows.md](clion-setup-windows.md) — toolchain and IDE setup.
