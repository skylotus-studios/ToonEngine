//============================================================================
//  core/renderer.h: ToonEngine's rendering seam.
//
//  This is the ONE header the rest of the engine talks to for GPU work. Every
//  backend detail (currently Diligent Engine on Vulkan) lives behind it in
//  renderer.cpp via PIMPL, so neither a Diligent type nor a Diligent header
//  escapes into engine or game code. Swapping the backend (or porting to a
//  console) becomes "write another renderer_*.cpp", not a rewrite.
//============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "core/math.h" // toon::Vec3/Quat (plain, Diligent-free)

// Forward-declared so this header pulls in neither GLFW nor any Diligent header.
struct GLFWwindow;

namespace toon {

    // --- Opaque GPU resource handles -------------------------------------------
    // The engine's vocabulary for GPU resources: plain 32-bit ids (0 is always the
    // null/invalid handle). The mapping from id to the underlying backend resource
    // lives entirely inside the renderer. Resource-creation APIs land with the
    // shader/pipeline work on the roadmap; the types are defined now so the seam's
    // contract is explicit: "the engine names resources, never Diligent objects."
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
    // DiligentTools, no GPU device needed). Returns false on failure (details go to
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

        // 2D editor mode (roadmap #14): true locks the viewport to an orthographic,
        // sprite-facing view (SetCamera branches its projection build on this) instead of
        // the perspective view above. orthoHeight is the orthographic analog of fovY: since
        // nothing shrinks with distance in an orthographic projection, it directly names the
        // world-space vertical extent the view shows, and zoom (CameraZoom) scales it instead
        // of distance while this is set.
        bool orthographic = false;
        float orthoHeight = 10.0f;

        // Editor-control tuning (used by core/camera.h, not read by the renderer).
        float lookSensitivity = 0.005f; // radians per pixel (orbit)
        float panSensitivity = 0.0015f; // world units per pixel, per unit of distance (pan)
        float zoomSpeed = 0.12f;        // fraction of distance per scroll notch (zoom)
        float moveSpeed = 6.0f;         // world units per second (fly)
    };

    // Per-object toon look, passed to DrawMesh. Lives in the seam so the debug UI
    // can drive it live and so each object in a scene can differ. (The light is
    // global scene state; see SetLight.)
    struct Material {
        Vec3 baseColor = {0.85f, 0.30f, 0.35f};    // albedo
        Vec3 outlineColor = {0.05f, 0.05f, 0.07f}; // rim color
        float outlineWidth = 0.03f;                // object-space extrusion
        float bands = 4.0f;                        // number of shading bands
        float ambient = 0.25f;                     // shadow-side floor (0 = black)
        float roughness = 0.9f;                    // SSR: low = reflective, high = matte
    };

    // Per-object placement. `rotation` is a quaternion (identity = no rotation); use
    // core/math.h's QuatFromEuler/QuatToEuler to edit it as Euler XYZ degrees (e.g. the
    // Inspector); that conversion applies X, then Y, then Z. Scale may be non-uniform:
    // DrawMesh derives an inverse-transpose normal matrix so shading, the G-buffer
    // normals, and the outline width all stay correct under any scale.
    struct Transform {
        Vec3 position = {0.0f, 0.0f, 0.0f};
        Quat rotation;
        Vec3 scale = {1.0f, 1.0f, 1.0f};
    };

    // Which of a skinned model's animations to sample, and when, for one DrawModel/
    // DrawModelShadow call. `clipIndex` indexes the model's own animation list (see
    // GetModelAnimationCount/Name); -1 plays no animation (bind pose). `time`/`prevTime`
    // are seconds into the clip, this frame and last -- both are needed because motion
    // vectors on an animated character depend on the bone motion, not just the object's
    // own world-matrix motion (see Renderer::DrawModel). A null AnimationState* (the
    // default at every call site) means "not animated": DrawModel/DrawModelShadow fall
    // back to the model's ordinary (possibly unskinned) draw path unchanged.
    struct AnimationState {
        int32_t clipIndex = -1;
        float time = 0.0f;
        float prevTime = 0.0f;
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
        // (fill maxes near the base color, < 1.0) the default threshold sits below 1.0.
        // Otherwise nothing is bright enough to bloom. Raise it toward/above 1.0 once
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
        bool ssaoTemporal = true;  // temporal accumulation, denoises the AO; safe now
                                   // that the scene writes real motion vectors

        // App-computed (not a Debug-panel toggle): true whenever temporal history
        // shouldn't be trusted: an active gizmo/UI interaction, or anything
        // continuously animating (Spin). Forces SSAO/TAA to drop accumulated history for
        // the duration. The Spin case needed real investigation: a rotating silhouette
        // is a view-dependent contour, not a fixed set of vertices, so no per-vertex
        // motion vector can fully represent its true motion; at Spin's slow default rate
        // the resulting per-frame error is small enough to slip under DiligentFX's own
        // motion-based history-distrust threshold (a compiled-in shader constant, not
        // exposed to us), so it compounds through up to 16 frames of heavy history trust
        // into a persistent ghost that never resolves on its own. ResetAccumulation is
        // the one sanctioned lever available, so: never accumulate while anything is
        // continuously moving.
        bool suppressTemporalHistory = false;

        // Depth of field (DiligentFX's DepthOfField via PostFXContext). Blurs by depth-
        // based circle of confusion; uses the motion vectors for temporal CoC smoothing.
        // Focus/aperture live in the camera attribs (see FillCameraAttribs).
        bool dof = false;           // off by default (it's a strong look)
        float dofFocusDist = 10.5f; // world-space distance in sharp focus (~the objects)
        float dofFStop = 6.0f;      // aperture: smaller = shallower focus = more blur
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

        // Cascaded shadow maps (Diligent's ShadowMapManager, forward-rendered, not a
        // PostFXContext effect). Casts shadows from the scene light onto every cel-shaded
        // surface; see Renderer::BeginShadowPass / DrawMeshShadow / DrawModelShadow.
        bool shadows = true;
    };

    // One vertex of a screen-space UI batch (DrawUI, roadmap #17). `pos` is in PIXELS with the
    // origin at the top-left of the window (the shader maps it to NDC), so the UI layer above the
    // seam (core/ui/) works purely in pixels. `color` is straight (non-premultiplied) RGBA. `mode`
    // selects the shading: 0 = solid fill (emit `color`); 1 = MSDF glyph (`uv` = atlas coord,
    // median distance -> coverage, times `color`); 2 = rounded rect + SDF border (`uv` = pixel
    // offset from the rect center, `params` = (halfW, halfH, cornerRadius, borderThickness), fill =
    // `color`, border = `borderColor`). The UI layer tessellates every rect/glyph into these -- two
    // triangles (6 verts) per quad, no index buffer -- and the renderer uploads and draws them.
    struct UIVertex {
        Vec2 pos;
        Vec2 uv;
        Vec4 color;
        float mode = 0.0f;  // 0 = solid fill, 1 = MSDF text, 2 = rounded rect + border
        Vec4 params{};      // mode 2: (halfW, halfH, cornerRadius, borderThickness), pixels
        Vec4 borderColor{}; // mode 2: border color (zero for modes 0/1)
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
        //
        // `strictValidation`, default false (every existing call site is unchanged): forces
        // Vulkan validation to its strictest level regardless of build config (Debug builds
        // already turn on a lighter default validation level; see docs/architecture.md-adjacent
        // reasoning in renderer.cpp's own comment) and installs a debug-message callback that
        // counts errors/warnings, read back via ValidationErrorCount/ValidationWarningCount
        // below. Used by app/headless_render.h's --headless-render mode; the editor and the
        // normal player never pass true, so their behavior is byte-identical to before this
        // parameter existed.
        bool Init(GLFWwindow *window, bool strictValidation = false);
        void Shutdown();

        // True once Init has successfully created the graphics device. False before Init, after a
        // failed Init, and after Shutdown -- and permanently in a run that never calls Init at all
        // (app/sim_runtime.h's --sim-only mode, which asserts on this every tick to prove no
        // device was created rather than merely trusting that it avoided the call). This is a
        // lifecycle query on the seam's OWN state, not a Diligent call wrapped one-to-one; every
        // device-touching entry point below early-outs when it returns false, so a device-less
        // Renderer is inert rather than a crash.
        bool HasDevice() const;

        // --- Metrics (app/metrics.h), --headless-render only --------------------
        // Cumulative counts since the debug-message callback was installed (Init's
        // strictValidation), for app/metrics.h's vulkan.validation_errors/warnings and
        // app/headless_render.h's nonzero-exit-on-error requirement. Always 0 when Init was
        // never called with strictValidation=true.
        uint32_t ValidationErrorCount() const;
        uint32_t ValidationWarningCount() const;

        // Draw calls / PSO binds issued so far THIS FRAME (reset at the top of
        // BeginShadowPass, the first call in a frame's real sequence) -- app/metrics.h's
        // render.draw_calls/pso_switches. Read after RenderHUD, before EndFrame, so both
        // reflect the whole frame just built.
        uint32_t DrawCallCount() const;
        uint32_t PSOSwitchCount() const;

        // Reads the swap chain's CURRENT back buffer (whatever the last BeginFrame/EndScene/
        // DrawUI sequence left in it) and writes it to `path` as a PNG. Call AFTER RenderHUD and
        // BEFORE EndFrame -- EndFrame's Present() transitions the back buffer out of a
        // CPU-readable layout. Returns false (logs to stderr) on failure; false with no device
        // (HasDevice() is false) rather than a crash, same contract as every other device-backed
        // entry point on this class.
        bool CaptureFrameToPNG(const char *path) const;

        // Per-frame: bind the back buffer and clear it, then present.
        void BeginFrame(const Color &clearColor);
        void EndFrame();

        // Keep the swap chain matched to the window's framebuffer size.
        void Resize(uint32_t width, uint32_t height);

        // Manually re-check every shader source file and recompile whatever changed (roadmap
        // #10). In a Debug build a file-system watcher already does this automatically once
        // per frame inside BeginFrame; this is a fallback trigger for the Settings panel's
        // "Reload Now" button. Returns how many shaders/pipelines were reloaded -- always 0 in
        // a Release build, where hot-reload is compiled out entirely.
        uint32_t ReloadShaders();

        // --- Scene: meshes + toon draw ------------------------------------------
        // Upload a mesh once; returns a handle (MeshHandle::Invalid on failure).
        MeshHandle CreateMesh(const Vertex *vertices, uint32_t vertexCount, const uint32_t *indices,
                              uint32_t indexCount);

        // Local-space (object-space) axis-aligned bounds, computed once at creation/load; the
        // app layer's mouse-pick (app/picking.cpp) transforms these by an entity's worldMatrix to
        // ray-test it. False (out-params untouched) for an invalid handle.
        bool GetMeshBounds(MeshHandle mesh, Vec3 &outMin, Vec3 &outMax) const;
        bool GetModelBounds(ModelHandle model, Vec3 &outMin, Vec3 &outMax) const;

        // Per-frame scene state (set between BeginFrame and the DrawMesh calls).
        void SetCamera(const Camera &camera);

        // Global scene light (single directional light). `color`/`intensity` are
        // premultiplied together before upload: pass intensity > 1 to push it into HDR
        // (bloom will pick it up).
        void SetLight(const Vec3 &directionToLight, const Vec3 &color, float intensity);

        // Current view + projection (as of the last SetCamera), for the editor's transform gizmo
        // (ImGuizmo). Handed out as plain Mat4 so the app/gizmo stay Diligent-free.
        void GetViewProj(Mat4 &view, Mat4 &proj) const;

        // Unproject a viewport pixel (as of the last SetCamera) to a world-space ray: origin on
        // the near plane, outDir normalized. `vpW`/`vpH` are the viewport's pixel size (today the
        // whole window, since the scene renders fullscreen behind the passthrough dockspace).
        // For mouse-pick (app/picking.cpp): feed ImGui's mouse position + io.DisplaySize.
        void ScreenPointToRay(float mouseX, float mouseY, float vpW, float vpH, Vec3 &outOrigin,
                               Vec3 &outDir) const;

        // Draw a mesh with the toon pipeline (outline pass + banded fill pass).
        // `prevTransform` is the object's placement *last* frame; it drives the motion
        // vectors SSAO temporal accumulation / DoF consume. Pass the same value as
        // `transform` for a static object (no motion).
        void DrawMesh(MeshHandle mesh, const Transform &transform, const Transform &prevTransform,
                      const Material &material);

        // Draw with a pre-composed world matrix (+ last frame's, for motion vectors), the
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
        void DrawModelShadow(ModelHandle model, const Mat4 &world, const AnimationState *anim = nullptr);
        void EndShadowPass();

        // --- Scene: glTF models -------------------------------------------------
        // Load a glTF/GLB model via DiligentTools' loader (Diligent::GLTF::Model owns the
        // GPU buffers + textures). Returns ModelHandle::Invalid on failure. Every model is
        // loaded requesting the same vertex attributes (position/normal/uv, plus joints/
        // weights in a second buffer slot); a file with no skin simply leaves that second
        // buffer unread by its (unskinned) draw path -- see ModelHasSkin.
        ModelHandle LoadModel(const char *path);

        // True if the loaded model has at least one skin (glTF's rigging data) -- i.e. it's
        // eligible to be drawn animated. Gates the Properties panel's "Add Animation" button.
        bool ModelHasSkin(ModelHandle model) const;

        // A model's own animation clip list, for a UI picker (e.g. Properties panel). Index
        // is what AnimationState::clipIndex selects. False/0/"" for an invalid handle or an
        // out-of-range index.
        uint32_t GetModelAnimationCount(ModelHandle model) const;
        std::string GetModelAnimationName(ModelHandle model, uint32_t index) const;
        float GetModelAnimationDuration(ModelHandle model, uint32_t index) const;

        // Draw a loaded model cel-shaded (textured fill; inverted-hull outline for a skinned
        // model too, once animated -- see AnimationState). `style` supplies the shared look
        // (bands / ambient / roughness) and its `baseColor` is a global tint over each
        // primitive's glTF base color (default white = untinted). Motion vectors come from
        // transform vs prevTransform (object motion) and, when `anim` is non-null, from the
        // bone motion between anim->time and anim->prevTime too.
        void DrawModel(ModelHandle model, const Transform &transform, const Transform &prevTransform,
                       const Material &style, const AnimationState *anim = nullptr);

        // Draw a loaded model with a pre-composed world matrix (see DrawMesh's Mat4 overload).
        void DrawModel(ModelHandle model, const Mat4 &world, const Mat4 &prevWorld, const Material &style,
                       const AnimationState *anim = nullptr);

        // --- Textures (editor UI: asset thumbnails/previews; also sprite textures) ---
        // Decodes PNG/JPG/BMP/TGA via DiligentTools' TextureLoader. `srgb` selects the
        // texture's source color space: false (default) for an asset browser thumbnail,
        // composited by ImGui's own gamma-space shader; true for a sprite texture (see
        // DrawSprite below), which composites into the linear HDR scene like every other
        // draw and needs the linearize-on-sample an sRGB view gives it.
        TextureHandle LoadTexture(const char *path, bool srgb = false); // TextureHandle::Invalid on failure
        void DestroyTexture(TextureHandle texture);

        // An opaque id ImGui::Image can draw (cast to ImTextureID at the call site; this
        // header stays ImGui-free). 0 for an invalid handle.
        uint64_t GetTextureImGuiID(TextureHandle texture) const;

        // Pixel dimensions, for sizing a preview. Left untouched (0) for an invalid handle.
        void GetTextureSize(TextureHandle texture, uint32_t &width, uint32_t &height) const;

        // --- 2D sprites (roadmap #13) --------------------------------------------
        // Draw a flat, textured, alpha-blended quad at `world` (transform-oriented -- no
        // billboarding), unlit. `prevWorld` is the quad's placement LAST frame, for its
        // motion vector (same convention as DrawMesh/DrawModel; pass the same value for a
        // static sprite). `tint` multiplies the sampled texel (straight alpha; a pixel
        // under 0.01 alpha is discarded). `uvRect` is an atlas sub-rect (xy = offset, zw =
        // scale; {0,0,1,1} = the whole texture); apply flipX/flipY (SpriteComponent, core/
        // scene/scene.h) by negating the relevant axis's offset/scale before calling this,
        // not here. Depth-tested against opaque geometry, writes depth + its own G-buffer
        // normal/roughness/motion (so later depth- and G-buffer-reading passes -- the editor
        // grid, SSR/SSAO/TAA -- see the sprite, not whatever it occludes), color alpha-
        // blended: call once per sprite, entities pre-sorted back-to-front (farthest first)
        // by the caller -- see docs/architecture.md's "Transparent sprite pass". Call AFTER
        // the opaque DrawMesh/DrawModel calls and BEFORE EndScene() (needs the still-bound
        // G-buffer + scene depth); a TextureHandle::Invalid is silently skipped.
        void DrawSprite(const Mat4 &world, const Mat4 &prevWorld, TextureHandle texture, const Vec4 &tint,
                        const Vec4 &uvRect);

        // Post-processing. Set params, then EndScene() resolves the HDR scene to the
        // back buffer (call after the DrawMesh calls, before the UI overlay).
        void SetPostParams(const PostParams &params);
        void EndScene();

        // --- Debug drawing (M2.1: collider wireframes) --------------------------
        // Draw a world-space line list -- e.g. core/physics.h's ColliderWireframe output.
        // `points` is `count` LOCAL-space positions, consecutive pairs forming one segment
        // ([0]-[1], [2]-[3], ...); `world` places them. Renders directly onto the resolved
        // back buffer, the same target the ImGui overlay draws onto next -- so call this
        // AFTER EndScene() and BEFORE BeginUI(). No depth test: always on top of the scene,
        // so a collider is never hidden inside the mesh it belongs to.
        void DrawWireframe(const Mat4 &world, const Vec3 *points, uint32_t count, const Color &color);

        // --- Editor backdrop: sky gradient + ground grid (roadmap #12) ----------
        // Two-color vertical gradient behind the scene, lerped by the world-space view ray's
        // Y direction (so the horizon stays level as the camera pitches, not tied to screen
        // Y). Call AFTER BeginFrame and BEFORE the entity DrawMesh/DrawModel calls, so opaque
        // geometry draws over it; it writes the full HDR G-buffer (color + zeroed normal/
        // motion) at the far depth BeginFrame already cleared to.
        void DrawSky(const Color &top, const Color &bottom);

        // Infinite ground grid on the XZ (Y=0) plane, built on DiligentFX's
        // CoordinateGridRenderer: per-pixel ray/plane intersection, antialiased multi-level-
        // of-detail lines, colored X/Z axes. Occludes itself by READING the finished scene
        // depth buffer (not by writing its own), so call AFTER EndScene() -- same call-timing
        // contract as DrawWireframe -- and BEFORE BeginUI().
        void DrawGrid();

        // --- In-game UI overlay (roadmap #17) -----------------------------------
        // Draw a screen-space UI batch (tinted quads + MSDF text) straight onto the resolved
        // back buffer -- same after-EndScene / before-BeginUI timing contract as DrawWireframe/
        // DrawGrid above, and the same LDR target the ImGui overlay uses (UI never runs through
        // bloom/tone-map). `vertices` is a triangle list, 6 verts per quad, in pixel space (see
        // UIVertex); `atlas` is the MSDF font atlas glyph quads sample (TextureHandle::Invalid
        // binds a 1x1 white fallback, so a solid-only batch needs no font loaded); `pixelRange`
        // is that atlas's MSDF distance range in texels (from its metrics), for the shader's
        // screen-space edge anti-aliasing. A zero vertexCount is a no-op.
        void DrawUI(const UIVertex *vertices, uint32_t vertexCount, TextureHandle atlas, float pixelRange);

        // --- Debug/editor UI (Dear ImGui) ---------------------------------------
        // Diligent's ImGui renderer backend (ImGuiImplDiligent) is confined to
        // renderer.cpp same as everything else; main.cpp only sees these four
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
        bool CreateSkinnedModelPipeline();                            // animated glTF model fill/outline PSOs
        bool CreatePostPipeline();                                    // HDR tone-map resolve PSO
        bool CreatePostFX();                                          // PostFXContext + Bloom + SSAO effects
        bool CreateOffscreenTargets(uint32_t width, uint32_t height); // HDR color + normal + depth + motion
        bool CreateShadowMap();                                       // ShadowMapManager + depth-only PSOs
        bool CreateWireframePipeline();                               // debug line-list PSO (DrawWireframe)
        bool CreateSkyPipeline();                                     // sky-gradient fullscreen PSO (DrawSky)
        bool CreateGridRenderer();                                    // DiligentFX CoordinateGridRenderer (DrawGrid)
        bool CreateSpritePipeline();                                  // transparent textured-quad PSO (DrawSprite)
        bool CreateUIPipeline();                                       // screen-space UI quad + MSDF text PSO (DrawUI)

        // Roadmap #11 (skeletal animation): grow the shared skinning joints buffer (never
        // shrink it) to hold at least `neededElements` bone matrices, re-pointing every
        // skinned draw's g_Joints binding at the new buffer when it actually grows. Called
        // from DrawModel/DrawModelShadow before a skinned draw's own joint-matrix upload.
        void EnsureJointsBufferCapacity(uint32_t neededElements);

        struct Impl; // defined in renderer.cpp; hides all Diligent types
        Impl *m_impl = nullptr;
    };

} // namespace toon
