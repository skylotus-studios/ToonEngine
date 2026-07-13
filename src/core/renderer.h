//============================================================================
//  core/renderer.h — ToonEngine's rendering seam.
//
//  This is the ONE header the rest of the engine talks to for GPU work. Every
//  backend detail (currently Diligent Engine on Vulkan) lives behind it in
//  renderer.cpp via PIMPL, so no Diligent type — and no Diligent header —
//  escapes into engine or game code. Swapping the backend (or porting to a
//  console) becomes "write another renderer_*.cpp", not a rewrite.
//============================================================================
#pragma once

#include <cstdint>

#include "core/math.h" // toon::Vec3 (plain, Diligent-free)

// Forward-declared so this header pulls in neither GLFW nor any Diligent header.
struct GLFWwindow;

namespace toon {

    // --- Opaque GPU resource handles -------------------------------------------
    // The engine's vocabulary for GPU resources: plain 32-bit ids (0 is always the
    // null/invalid handle). The mapping from id to the underlying backend resource
    // lives entirely inside the renderer. Resource-creation APIs land with the
    // shader/pipeline work on the roadmap; the types are defined now so the seam's
    // contract — "the engine names resources, never Diligent objects" — is explicit.
    enum class TextureHandle : uint32_t { Invalid = 0 };
    enum class BufferHandle : uint32_t { Invalid = 0 };
    enum class ShaderHandle : uint32_t { Invalid = 0 };
    enum class PipelineHandle : uint32_t { Invalid = 0 };
    enum class MeshHandle : uint32_t { Invalid = 0 };
    enum class ModelHandle : uint32_t { Invalid = 0 }; // a loaded glTF/GLB asset

    struct Color {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    };

    // Sets the OS window/taskbar icon from an image file (PNG/TGA/etc, decoded via
    // DiligentTools — no GPU device needed). Returns false on failure (details go to
    // stderr). Call any time after the window is created.
    bool SetWindowIcon(GLFWwindow *window, const char *path);

    // Themes the native title bar to match the ImGui editor instead of the OS's stock
    // white/light bar: enables Windows' dark window chrome (frame + system buttons), and
    // on Windows 11 22H2+ sets the exact caption background/text color via DWM (older
    // Windows just keeps the dark-mode default, no exact color match). `background`/
    // `text` are 0-1 RGB, e.g. the active theme's MenuBarBg/Text colors. Call once after
    // the window is created and again whenever the editor theme changes. No-op (returns
    // false) on non-Windows platforms.
    bool SetTitleBarTheme(GLFWwindow *window, Color background, Color text);

    // --- Scene vocabulary -------------------------------------------------------
    // Plain, backend-agnostic types the engine speaks in. The renderer converts
    // them to Diligent math/resources behind the seam.

    // One mesh vertex. The input layout in renderer.cpp mirrors this (three
    // tightly-packed float3s). `normal` shades the fill (may be faceted/per-face);
    // `smoothNormal` is the averaged normal the outline hull extrudes along, so hard
    // edges (e.g. a cube's corners) stay closed instead of splitting apart. For
    // smooth meshes the two are identical.
    struct Vertex {
        Vec3 position;
        Vec3 normal;
        Vec3 smoothNormal;
    };

    // Editor camera: orbits a movable `pivot` at `distance`, yaw/pitch. The renderer builds
    // the view + (Vulkan-correct) projection from these; keeping the matrix math on the
    // Diligent side avoids leaking NDC/handedness conventions across the seam. The orbit /
    // pan / zoom / fly *controls* live in core/camera.h and mutate these fields.
    struct Camera {
        Vec3 pivot = {0.0f, 0.0f, 0.0f}; // orbit target (pan + fly move it)
        float distance = 10.0f;          // orbit radius from the pivot (zoom changes it)
        float yaw = 0.0f;                // radians, around +Y
        float pitch = 0.25f;             // radians, around +X
        float fovY = 1.0472f;            // vertical field of view (~60 degrees)
        float nearZ = 0.1f;
        float farZ = 100.0f;

        // Editor-control tuning (used by core/camera.h — not read by the renderer).
        float lookSensitivity = 0.005f; // radians per pixel (orbit)
        float panSensitivity = 0.0015f; // world units per pixel, per unit of distance (pan)
        float zoomSpeed = 0.12f;        // fraction of distance per scroll notch (zoom)
        float moveSpeed = 6.0f;         // world units per second (fly)
    };

    // Per-object toon look, passed to DrawMesh. Lives in the seam so the debug UI
    // can drive it live and so each object in a scene can differ. (The light is
    // global scene state — see SetLight.)
    struct Material {
        Vec3 baseColor = {0.85f, 0.30f, 0.35f};    // albedo
        Vec3 outlineColor = {0.05f, 0.05f, 0.07f}; // rim color
        float outlineWidth = 0.03f;                // object-space extrusion
        float bands = 4.0f;                        // number of shading bands
        float ambient = 0.25f;                     // shadow-side floor (0 = black)
        float roughness = 0.9f;                    // SSR: low = reflective, high = matte
    };

    // Per-object placement. Rotation is applied X, then Y, then Z. Scale may be
    // non-uniform: DrawMesh derives an inverse-transpose normal matrix so shading,
    // the G-buffer normals, and the outline width all stay correct under any scale.
    struct Transform {
        Vec3 position = {0.0f, 0.0f, 0.0f};
        Vec3 rotationEuler = {0.0f, 0.0f, 0.0f}; // radians
        Vec3 scale = {1.0f, 1.0f, 1.0f};
    };

    // HDR resolve / tone-mapping controls. The scene renders to an offscreen HDR
    // target; EndScene runs the optional bloom pass, then resolves to the back buffer
    // with exposure + a filmic tone-map curve.
    struct PostParams {
        float exposure = 1.0f; // linear multiplier applied before tone mapping
        bool toneMap = true;   // ACES filmic tone map (vs. plain clamp)

        // Bloom (DiligentFX's Bloom effect via PostFXContext). Bright pixels bleed a
        // soft glow into their surroundings. NOTE: the threshold/knee run on the *raw*
        // HDR scene, before exposure + tone mapping, so on this LDR-ranged toon scene
        // (fill maxes near the base color, < 1.0) the default threshold sits below 1.0
        // — otherwise nothing is bright enough to bloom. Raise it toward/above 1.0 once
        // the scene carries real over-bright (emissive) values.
        bool bloom = true;
        float bloomIntensity = 0.5f; // strength of the added glow
        float bloomThreshold = 0.6f; // min max(r,g,b) brightness that blooms
        float bloomSoftKnee = 0.5f;  // softens the threshold edge (0 = hard cutoff)
        float bloomRadius = 0.75f;   // glow spread (fraction of the mip chain, 0.3–0.85)

        // Screen-space ambient occlusion (DiligentFX's SSAO via PostFXContext). Darkens
        // creases / contact areas. Unlike bloom, SSAO reads real depth + normals + camera,
        // so enabling it fills in the camera attribs PostFXContext otherwise gets as zeros.
        bool ssao = true;
        float ssaoStrength = 1.0f; // composite strength (0 = off, 1 = full occlusion)
        float ssaoRadius = 1.5f;   // world-space occlusion radius
        bool ssaoTemporal = true;  // temporal accumulation — denoises the AO; safe now
                                   // that the scene writes real motion vectors

        // App-computed (not a Debug-panel toggle): true whenever temporal history
        // shouldn't be trusted — an active gizmo/UI interaction, or anything
        // continuously animating (Spin). Forces SSAO/TAA to drop accumulated history for
        // the duration. The Spin case needed real investigation: a rotating silhouette
        // is a view-dependent contour, not a fixed set of vertices, so no per-vertex
        // motion vector can fully represent its true motion; at Spin's slow default rate
        // the resulting per-frame error is small enough to slip under DiligentFX's own
        // motion-based history-distrust threshold (a compiled-in shader constant, not
        // exposed to us), so it compounds through up to 16 frames of heavy history trust
        // into a persistent ghost that never resolves on its own. ResetAccumulation is
        // the one sanctioned lever available, so: never accumulate while anything is
        // continuously moving. See MEMORY.md ("Bugs found dogfooding") for the full
        // investigation.
        bool suppressTemporalHistory = false;

        // Depth of field (DiligentFX's DepthOfField via PostFXContext). Blurs by depth-
        // based circle of confusion; uses the motion vectors for temporal CoC smoothing.
        // Focus/aperture live in the camera attribs (see FillCameraAttribs).
        bool dof = false;           // off by default (it's a strong look)
        float dofFocusDist = 10.5f; // world-space distance in sharp focus (~the objects)
        float dofFStop = 6.0f;      // aperture — smaller = shallower focus = more blur
        float dofMaxCoC = 0.015f;   // max circle-of-confusion (blur size), texture-UV units

        // Temporal anti-aliasing (DiligentFX's TemporalAntiAliasing). Jitters the camera
        // sub-pixel each frame and accumulates via motion vectors. Off by default: it
        // softens the crisp cel edges + outlines that define the toon look (but it does
        // clean up SSAO/DoF temporal noise).
        bool taa = false;

        // Screen-space reflections (DiligentFX's ScreenSpaceReflection). Ray-marches the
        // depth buffer to reflect the scene in smooth surfaces (the ground). Per-object
        // roughness (Material::roughness) gates it: only low-roughness pixels reflect.
        // We add the reflection radiance in the resolve (a simplified composite, no PBR
        // BRDF/env-map). Off by default.
        bool ssr = false;
        float ssrStrength = 0.6f; // how strongly the reflection is added

        // Cascaded shadow maps (Diligent's ShadowMapManager, forward-rendered — not a
        // PostFXContext effect). Casts shadows from the scene light onto every cel-shaded
        // surface; see Renderer::BeginShadowPass / DrawMeshShadow / DrawModelShadow.
        bool shadows = true;
    };

    // --- Renderer ---------------------------------------------------------------
    // Owns the graphics device, immediate context, and swap chain, and drives the
    // per-frame lifecycle. Backend-agnostic by construction (see file header).
    class Renderer {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        // Create the device + swap chain for `window` (a GLFW window created with
        // the GLFW_NO_API hint). Returns false on failure (details go to stderr).
        bool Init(GLFWwindow *window);
        void Shutdown();

        // Per-frame: bind the back buffer and clear it, then present.
        void BeginFrame(const Color &clearColor);
        void EndFrame();

        // Keep the swap chain matched to the window's framebuffer size.
        void Resize(uint32_t width, uint32_t height);

        // --- Scene: meshes + toon draw ------------------------------------------
        // Upload a mesh once; returns a handle (MeshHandle::Invalid on failure).
        MeshHandle CreateMesh(const Vertex *vertices, uint32_t vertexCount, const uint32_t *indices,
                              uint32_t indexCount);

        // Per-frame scene state (set between BeginFrame and the DrawMesh calls).
        void SetCamera(const Camera &camera);

        // Global scene light (single directional light). `color`/`intensity` are
        // premultiplied together before upload — pass intensity > 1 to push it into HDR
        // (bloom will pick it up).
        void SetLight(const Vec3 &directionToLight, const Vec3 &color, float intensity);

        // Current view + projection (as of the last SetCamera), for the editor's transform gizmo
        // (ImGuizmo). Handed out as plain Mat4 so the app/gizmo stay Diligent-free.
        void GetViewProj(Mat4 &view, Mat4 &proj) const;

        // Draw a mesh with the toon pipeline (outline pass + banded fill pass).
        // `prevTransform` is the object's placement *last* frame — it drives the motion
        // vectors SSAO temporal accumulation / DoF consume. Pass the same value as
        // `transform` for a static object (no motion).
        void DrawMesh(MeshHandle mesh, const Transform &transform, const Transform &prevTransform,
                      const Material &material);

        // Draw with a pre-composed world matrix (+ last frame's, for motion vectors) — the
        // path the scene graph uses, since it composes world transforms down the hierarchy.
        void DrawMesh(MeshHandle mesh, const Mat4 &world, const Mat4 &prevWorld, const Material &material);

        // --- Cascaded shadow maps (Diligent's ShadowMapManager) -----------------
        // Call once per frame, AFTER SetCamera + SetLight and BEFORE BeginFrame (the shadow
        // pass renders into its own depth-only targets, separate from the main G-buffer).
        // Returns the cascade count to loop over (0 if shadows are off in PostParams or the
        // scene has no light). For each cascade: BeginShadowCascade(i), then DrawMeshShadow/
        // DrawModelShadow for every shadow-casting entity, then move to the next cascade.
        // EndShadowPass finalizes (a no-op today; present for symmetry / future filtering
        // modes). The main pass's later DrawMesh/DrawModel calls read the finished shadow map.
        uint32_t BeginShadowPass();
        void BeginShadowCascade(uint32_t cascadeIndex);
        void DrawMeshShadow(MeshHandle mesh, const Mat4 &world);
        void DrawModelShadow(ModelHandle model, const Mat4 &world);
        void EndShadowPass();

        // --- Scene: glTF models -------------------------------------------------
        // Load a glTF/GLB model via DiligentTools' loader (Diligent::GLTF::Model owns the
        // GPU buffers + textures). Returns ModelHandle::Invalid on failure.
        ModelHandle LoadModel(const char *path);

        // Draw a loaded model cel-shaded (textured fill; no inverted-hull outline yet).
        // `style` supplies the shared look — bands / ambient / roughness — and its
        // `baseColor` is a global tint over each primitive's glTF base color (default white
        // = untinted). Motion vectors come from transform vs prevTransform, like DrawMesh.
        void DrawModel(ModelHandle model, const Transform &transform, const Transform &prevTransform,
                       const Material &style);

        // Draw a loaded model with a pre-composed world matrix (see DrawMesh's Mat4 overload).
        void DrawModel(ModelHandle model, const Mat4 &world, const Mat4 &prevWorld, const Material &style);

        // --- Textures (editor UI: asset thumbnails/previews) --------------------
        // Not part of the toon draw path (materials don't carry textures yet) — this exists
        // so editor UI (the asset browser) can decode an image file and display it with
        // ImGui::Image. Decodes PNG/JPG/BMP/TGA via DiligentTools' TextureLoader.
        TextureHandle LoadTexture(const char *path); // TextureHandle::Invalid on failure
        void DestroyTexture(TextureHandle texture);

        // An opaque id ImGui::Image can draw (cast to ImTextureID at the call site — this
        // header stays ImGui-free). 0 for an invalid handle.
        uint64_t GetTextureImGuiID(TextureHandle texture) const;

        // Pixel dimensions, for sizing a preview. Left untouched (0) for an invalid handle.
        void GetTextureSize(TextureHandle texture, uint32_t &width, uint32_t &height) const;

        // Post-processing. Set params, then EndScene() resolves the HDR scene to the
        // back buffer (call after the DrawMesh calls, before the UI overlay).
        void SetPostParams(const PostParams &params);
        void EndScene();

        // --- Debug/editor UI (Dear ImGui) ---------------------------------------
        // Diligent's ImGui renderer backend (ImGuiImplDiligent) is confined to
        // renderer.cpp same as everything else — main.cpp only sees these four
        // calls. Dear ImGui itself is a plain UI library, not a Diligent type, so
        // engine/game code is free to include <imgui.h> and call ImGui:: directly
        // between BeginUI() and EndUI() to build UI.
        bool InitUI(GLFWwindow *window);
        void ShutdownUI();
        void BeginUI();
        void EndUI();

    private:
        // Internal setup steps (called from Init/Resize). Plain signatures so the
        // header stays Diligent-free.
        bool CreateToonPipeline();                                    // toon fill/outline PSOs + shared CB
        bool CreateModelPipeline();                                   // glTF model cel-fill PSO (+ albedo)
        bool CreatePostPipeline();                                    // HDR tone-map resolve PSO
        bool CreatePostFX();                                          // PostFXContext + Bloom + SSAO effects
        bool CreateOffscreenTargets(uint32_t width, uint32_t height); // HDR color + normal + depth + motion
        bool CreateShadowMap();                                       // ShadowMapManager + depth-only PSOs

        struct Impl; // defined in renderer.cpp — hides all Diligent types
        Impl *m_impl = nullptr;
    };

} // namespace toon
