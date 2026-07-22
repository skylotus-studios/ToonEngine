//============================================================================
//  app/editor_init.cpp: see editor_init.h.
//============================================================================
#include "app/editor_init.h"

#include "app/editor_state.h"
#include "core/input/action_map.h"
#include "core/input/binding_io.h"
#include "core/input/input_system.h"
#include "core/scene/scripts/spin_script.h"

#include <GLFW/glfw3.h>

#include "imgui.h"
#ifdef IMGUI_HAS_DOCK
#include "imgui_internal.h"
#endif
#include "IconsFontAwesome6.h" // ICON_FA_* glyph macros for the Font Awesome icon font merged below

#include <cstdint>
#include <cstdio>
#include <memory>

namespace toon {
    namespace {

        // Create + upload a procedural mesh from `desc`, and record `desc` on the entity so a
        // saved scene can regenerate this mesh on load (a procedural mesh has no source file).
        void SetPrimitive(Renderer &r, Entity &e, const PrimitiveDesc &desc) {
            e.primitive = desc;
            const MeshData m = MakePrimitiveMesh(desc);
            e.mesh = r.CreateMesh(m.vertices.data(), static_cast<uint32_t>(m.vertices.size()), m.indices.data(),
                                  static_cast<uint32_t>(m.indices.size()));
            if (e.mesh == MeshHandle::Invalid) { std::fprintf(stderr, "Failed to create mesh '%s'\n", e.name.c_str()); }
        }

        // Attach a Spin script (core/scene/scripts/spin_script.h) to entity `i` -- the script
        // lives on the entity itself, so it survives reparent/reload/Stop with no external
        // index bookkeeping to keep in sync.
        void AddSpin(Scene &scene, int i, Vec3 axis, float speed = 0.6f) {
            auto s = std::make_unique<SpinScript>();
            s->axis = axis;
            s->speed = speed;
            scene.entities[i].scripts.push_back({kSpinScriptName, std::move(s)});
        }

        // A real entity tree (core/scene/scene.h) instead of a hardcoded array. Root at index 0;
        // everything is a child of the root EXCEPT the satellite, which is parented to the cube
        // to demonstrate hierarchy composition (it orbits the cube as the cube spins).
        void SeedDemoScene(EditorState &state) {
            Renderer &renderer = state.renderer;
            Scene &scene = state.scene;
            EnsureSceneRoot(scene);

            // Ground plane beneath the objects (catches their SSAO contact shadows; no spin/outline).
            {
                Entity &e = scene.entities[AddEntity(scene, 0, "Ground")];
                SetPrimitive(renderer, e, PrimitiveDesc::Plane(5.0f));
                e.transform->position = {0.0f, -1.05f, 0.0f};
                e.material.baseColor = {0.60f, 0.60f, 0.63f};
                e.material.outlineWidth = 0.0f;
                e.material.roughness = 0.05f; // smooth -> reflective (SSR)

                // Static physics collider (M2.1): a thin box matching the plane's own 5.0 half-extent.
                // Its center sits at the same position as the (zero-thickness) visual plane, so its
                // top surface reads ~0.1 units ABOVE the rendered ground -- a collider has no local
                // offset from its entity today, so this small mismatch is a deliberate simplification,
                // not an oversight (a future collider-offset field would let it align exactly).
                e.collider = ColliderComponent{ColliderShape::Box, {5.0f, 0.1f, 5.0f}};
            }
            // Sphere: non-uniformly scaled into a spinning ellipsoid (exercises the normal matrix).
            {
                const int i = AddEntity(scene, 0, "Sphere");
                Entity &e = scene.entities[i];
                SetPrimitive(renderer, e, PrimitiveDesc::Sphere(1.0f, 32, 48));
                e.transform->position = {-2.8f, 0.0f, 0.0f};
                e.transform->scale = {1.5f, 0.8f, 1.0f};
                e.material = Material{{0.85f, 0.30f, 0.35f}, {0.24f, 0.05f, 0.08f}, 0.030f};
                e.material.roughness = 0.15f; // lightly glossy so SSR reflects on it
                AddSpin(scene, i, {0.0f, 1.0f, 0.0f});
            }
            // Cube: the satellite's parent.
            const int cubeIdx = AddEntity(scene, 0, "Cube");
            {
                Entity &e = scene.entities[cubeIdx];
                SetPrimitive(renderer, e, PrimitiveDesc::Cube(0.9f));
                e.material = Material{{0.30f, 0.45f, 0.85f}, {0.02f, 0.02f, 0.05f}, 0.050f};
                e.material.roughness = 0.15f;
                AddSpin(scene, cubeIdx, {0.5f, 1.0f, 0.0f});
            }
            // Satellite: a small sphere PARENTED to the cube (the hierarchy demo). It has no spin of
            // its own; it orbits the cube purely by inheriting the cube's spinning world transform.
            // Created right after the cube so the flat outliner (vector order) lists it directly under
            // its parent, keeping the scripted scene in pre-order, as the editor mutations always are.
            {
                Entity &e = scene.entities[AddEntity(scene, cubeIdx, "Satellite")];
                SetPrimitive(renderer, e, PrimitiveDesc::Sphere(0.22f, 16, 24));
                e.transform->position = {1.7f, 0.0f, 0.0f}; // offset from the cube (its parent)
                e.material = Material{{0.40f, 0.90f, 0.55f}, {0.03f, 0.07f, 0.04f}, 0.014f};
                e.material.roughness = 0.15f;
            }
            // Torus: also the audio demo emitter (M2.2), a looping, positional hum, so
            // orbiting the camera around it while Playing audibly pans/attenuates. Autoplay
            // starts it the moment Play begins (app/audio_glue.cpp's BuildAudioWorld).
            {
                const int i = AddEntity(scene, 0, "Torus");
                Entity &e = scene.entities[i];
                SetPrimitive(renderer, e, PrimitiveDesc::Torus(0.75f, 0.32f, 48, 24));
                e.transform->position = {2.8f, 0.0f, 0.0f};
                e.material = Material{{0.90f, 0.70f, 0.25f}, {0.32f, 0.20f, 0.03f}, 0.022f};
                e.material.roughness = 0.15f;
                AddSpin(scene, i, {1.0f, 0.0f, 0.0f});

                AudioSource audioSrc;
                audioSrc.clip = TOON_AUDIO_DIR "/demo_hum.wav";
                audioSrc.volume = 0.6f;
                audioSrc.loop = true;
                audioSrc.autoplay = true;
                audioSrc.spatial = true;
                audioSrc.maxDistance = 15.0f;
                e.audioSource = audioSrc;
            }
            // Falling primitives (M2.1 physics demo): dynamic rigid bodies, no spin script -- physics
            // owns their transform each Play tick, unlike the spinning showcase above. Dropped above
            // the ground at a clear spot (x=4) and stacked at increasing height, so pressing Play
            // makes them fall and land on one another as well as the ground -- the visible proof of
            // physics, the same role the spin demo (above) plays for native scripts.
            {
                const int i = AddEntity(scene, 0, "PhysicsCube1");
                Entity &e = scene.entities[i];
                SetPrimitive(renderer, e, PrimitiveDesc::Cube(0.4f));
                e.transform->position = {4.0f, 3.0f, 0.0f};
                e.material = Material{{0.25f, 0.80f, 0.35f}, {0.04f, 0.12f, 0.06f}, 0.018f};
                e.material.roughness = 0.4f;
                e.collider = ColliderComponent{ColliderShape::Box, {0.4f, 0.4f, 0.4f}};
                e.body = RigidBodyComponent{BodyType::Dynamic, 1.0f};
            }
            {
                const int i = AddEntity(scene, 0, "PhysicsSphere1");
                Entity &e = scene.entities[i];
                SetPrimitive(renderer, e, PrimitiveDesc::Sphere(0.35f, 24, 32));
                e.transform->position = {4.0f, 5.0f, 0.0f};
                e.material = Material{{0.90f, 0.55f, 0.15f}, {0.28f, 0.16f, 0.03f}, 0.015f};
                e.material.roughness = 0.3f;
                e.collider = ColliderComponent{ColliderShape::Sphere, {0.35f, 0.0f, 0.0f}};
                e.body = RigidBodyComponent{BodyType::Dynamic, 0.8f};
            }
            {
                const int i = AddEntity(scene, 0, "PhysicsCube2");
                Entity &e = scene.entities[i];
                SetPrimitive(renderer, e, PrimitiveDesc::Cube(0.3f));
                e.transform->position = {4.0f, 7.0f, 0.0f};
                e.material = Material{{0.55f, 0.30f, 0.80f}, {0.10f, 0.05f, 0.14f}, 0.014f};
                e.material.roughness = 0.4f;
                e.collider = ColliderComponent{ColliderShape::Box, {0.3f, 0.3f, 0.3f}};
                e.body = RigidBodyComponent{BodyType::Dynamic, 0.6f};
            }
            // Loaded glTF model (DiligentTools' loader): cel-shaded albedo + inverted-hull outline.
            const char *helmetPath = TOON_MODELS_DIR "/helmet.glb";
            const ModelHandle helmet = renderer.LoadModel(helmetPath);
            if (helmet != ModelHandle::Invalid) {
                const int i = AddEntity(scene, 0, "Helmet");
                Entity &e = scene.entities[i];
                e.model = helmet;
                e.modelPath = helmetPath; // so a saved scene can reload it (see core/scene/serializer.h)
                e.transform->position = {0.0f, 2.5f, 0.0f};
                e.transform->scale = {1.4f, 1.4f, 1.4f};
                e.material.baseColor = {1.0f, 1.0f, 1.0f}; // white tint (glTF supplies the color)
                e.material.outlineColor = {0.02f, 0.02f, 0.03f};
                e.material.outlineWidth = 0.04f;
                e.material.roughness = 0.5f;
                AddSpin(scene, i, {0.0f, 1.0f, 0.0f});
            }
            // Loaded, animated glTF model (roadmap #11: skeletal animation): the Khronos Fox
            // test asset, already under assets/models/ but unused before this. The
            // AnimationComponent is only attached if the file actually exposes a playable
            // clip -- defensive, since this is the first real skinned-model asset this engine
            // has loaded, rather than assuming its content sight-unseen.
            const char *foxPath = TOON_MODELS_DIR "/fox.glb";
            const ModelHandle fox = renderer.LoadModel(foxPath);
            if (fox != ModelHandle::Invalid) {
                const int i = AddEntity(scene, 0, "Fox");
                Entity &e = scene.entities[i];
                e.model = fox;
                e.modelPath = foxPath; // so a saved scene can reload it (see core/scene/serializer.h)
                e.transform->position = {-3.0f, 0.0f, 0.0f};
                // The Khronos Fox test asset's own mesh units are much larger than this
                // scene's other content (dwarfing the helmet at scale 1); 0.15 brings it to a
                // comparable size.
                e.transform->scale = {0.15f, 0.15f, 0.15f};
                e.material.baseColor = {1.0f, 1.0f, 1.0f}; // white tint (glTF supplies the color)
                e.material.outlineColor = {0.02f, 0.02f, 0.03f};
                e.material.outlineWidth = 0.03f;
                e.material.roughness = 0.6f;
                if (renderer.ModelHasSkin(fox) && renderer.GetModelAnimationCount(fox) > 0) {
                    AnimationComponent anim;
                    anim.clipIndex = 0; // play whatever the file's first clip is (e.g. Survey)
                    e.animation = anim;
                }
            }
            // Sprite (roadmap #13): reuses the window icon (already on disk under
            // TOON_SPRITES_DIR, which doubles as TOON_ICON_PATH's directory) as a demo
            // texture rather than adding a new binary asset just for this. Sits above the
            // cube/satellite pair, transform-oriented (identity rotation, no billboard -- the
            // roadmap item's scope): orbiting the camera past its edge visibly thins it to a
            // line, proof it isn't secretly facing the camera.
            {
                const int i = AddEntity(scene, 0, "Sprite");
                Entity &e = scene.entities[i];
                e.transform->position = {-1.4f, 1.6f, 0.0f};
                e.transform->scale = {1.2f, 1.2f, 1.2f};
                SpriteComponent sprite;
                sprite.texturePath = "icon.png"; // relative to TOON_SPRITES_DIR
                sprite.texture = renderer.LoadTexture(SpriteTexturePath(sprite.texturePath).c_str(), /*srgb=*/true);
                e.sprite = sprite;
            }
            // Sun: a directional light entity (no mesh/model, so the draw loop's isMesh/isModel
            // check skips it). Aimed by rotation (MakeLightTransform), reproducing the scene's old
            // fixed light direction exactly, so the default render is unchanged.
            {
                const int i = AddEntity(scene, 0, "Sun");
                Entity &e = scene.entities[i];
                e.transform = MakeLightTransform({0.0f, 4.0f, 0.0f}, {0.5f, 0.8f, -0.3f});
                e.light = LightComponent{};
            }

            // Start with the cube selected so the Inspector is populated on launch.
            scene.selected = cubeIdx;
        }

    } // namespace

    bool InitEditor(EditorState &state, GLFWwindow *window) {
        state.window = window;
        SetWindowIcon(window, TOON_ICON_PATH);

        if (!state.renderer.Init(window)) {
            std::fprintf(stderr, "Renderer init failed\n");
            return false;
        }

        // Physics (M2.1): Jolt's process-global setup (allocator/factory/type registry) happens
        // once here, alongside the renderer's own Init -- the world stays empty (no bodies)
        // until a Play/Step session calls BuildPhysicsWorld (app/physics_glue.h).
        if (!state.physicsWorld.Init()) { std::fprintf(stderr, "PhysicsWorld init failed\n"); }

        // Audio (M2.2): miniaudio's engine + its own device/audio thread, alongside physics'
        // own one-time setup above -- the audio world stays silent (no autoplay emitters
        // started) until a Play/Step session calls BuildAudioWorld (app/audio_glue.h).
        if (!state.audio.Init()) { std::fprintf(stderr, "AudioEngine init failed\n"); }

        // Install input callbacks BEFORE InitUI, so ImGui's GLFW backend chains ours instead of
        // overwriting them (see core/input/input_system.h's Init banner).
        Input::Init(window);

        // Seed the default editor bindings (camera fly/orbit + focus; see action_map.cpp's
        // RegisterDefaultEditorBindings), then let a saved assets/input.json override them if one
        // exists; otherwise write the defaults so the file exists next time. Same "load or create"
        // shape as scene save/load (core/scene/serializer.h).
        Input::RegisterDefaultEditorBindings();
        if (auto *editorBindings = Input::GetContext("editor")) {
            if (!Input::BindingIO::Load(TOON_INPUT_JSON, *editorBindings)) {
                Input::BindingIO::Save(TOON_INPUT_JSON, *editorBindings);
            }
        }

        if (!state.renderer.InitUI(window)) {
            std::fprintf(stderr, "Renderer UI init failed\n");
            state.renderer.Shutdown();
            return false;
        }

        // Editor-style docking: panels can be dragged to snap around the 3D view.
        // (ImGui is exempt from the renderer seam, so app code drives UI policy.)
        // Guarded on IMGUI_HAS_DOCK so the build stays green with a non-docking imgui.
        // Docking activates only when a docking-branch imgui is checked out.
#ifdef IMGUI_HAS_DOCK
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

        // Editor look: load the UI font (Bai Jamjuree) at the display's DPI scale, then apply the
        // starting theme. ImGui is seam-exempt and the Diligent backend advertises
        // RendererHasTextures (imgui 1.92 dynamic atlas), so adding the font here (after InitUI
        // created the context, before the first frame) is enough; the glyph texture uploads on
        // first draw. uiScale also drives ApplyTheme's ScaleAllSizes so the whole UI matches DPI.
        glfwGetWindowContentScale(window, &state.uiScale, &state.uiScaleY);
        ImGui::GetIO().Fonts->AddFontFromFileTTF(TOON_FONTS_DIR "/BaiJamjuree-Medium.ttf", 18.0f * state.uiScale);

        // Merge Font Awesome 6 solid's icon glyphs into that same font (MergeMode stitches them
        // into the range Bai Jamjuree just registered instead of starting a second font), so the
        // ICON_FA_* macros (ui/panels/file_browser.cpp) render inline with body text: same
        // baseline, same line height. GlyphMinAdvanceX gives every icon the same advance width
        // regardless of its natural glyph width, which keeps a column of mixed icons visually
        // aligned.
        ImFontConfig iconFontConfig;
        iconFontConfig.MergeMode = true;
        iconFontConfig.PixelSnapH = true;
        iconFontConfig.GlyphMinAdvanceX = 18.0f * state.uiScale;
        static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
        ImGui::GetIO().Fonts->AddFontFromFileTTF(TOON_FONTS_DIR "/fa-solid-900.ttf", 18.0f * state.uiScale,
                                                 &iconFontConfig, iconRanges);

        ApplyTheme(state.uiTheme, state.uiScale, window);

        // Route framebuffer resizes to the renderer's swap chain.
        glfwSetWindowUserPointer(window, &state.renderer);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int width, int height) {
            if (auto *r = static_cast<Renderer *>(glfwGetWindowUserPointer(w))) {
                r->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            }
        });

        SeedDemoScene(state);

        // Editor camera: driven by the mouse/keyboard in TickEditor (defaults: pivot at the
        // origin, distance 10, a slight downward pitch so the ground + its AO show).
        state.cameraDefault = state.camera; // for the "Reset camera" button

        // Scene serialization (core/scene/serializer.h): path field + Save/Load buttons live in
        // the Settings panel. `sceneStatus` echoes the last op's result in the UI (SaveScene/
        // LoadScene also log to the console) since this dev environment has no reliable console.
        std::snprintf(state.scenePathBuf, sizeof(state.scenePathBuf), "%s", TOON_SCENES_DIR "/default.scene");

        // Contents: browses assets/ with thumbnails; passive besides double-click, which
        // main.cpp routes through LoadSceneInto when the activated file is a .scene.
        InitFileBrowser(state.assetBrowser, TOON_ASSETS_DIR);

        state.lastTime = glfwGetTime();

        return true;
    }

} // namespace toon
