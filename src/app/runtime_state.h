#pragma once
//============================================================================
//  app/runtime_state.h: the engine-half shared state, common to the editor and the player.
//
//  A plain data bundle (no methods, no hidden internals), the same plain-struct-plus-free-
//  functions shape core/scene/scene.h's Scene uses. This is the state a shipped game needs
//  with no editor at all: the renderer, the physics/audio worlds, the scene, the camera, the
//  render style, and the application lifecycle. EditorState (app/editor_state.h) embeds one of
//  these and adds the editor-only fields (panels, gizmo, themes) on top.
//
//  The split (roadmap #15) is what lets app/runtime_tick.cpp + app/runtime_render.cpp +
//  app/runtime_init.cpp run without ui/panels/ or ImGui: they take a RuntimeState&, never an
//  EditorState&, so the ToonPlayer target can link them and nothing else.
//============================================================================
#include "app/app_state.h"
#include "core/audio/audio.h"
#include "core/physics/physics.h"
#include "core/rendering/renderer.h" // Renderer, Camera, Material, PostParams, Color
#include "core/scene/scene.h"

#include <cstdint>
#include <string>
#include <unordered_map>

struct GLFWwindow;

namespace toon {

    struct RuntimeState {
        GLFWwindow *window = nullptr;

        Renderer renderer;
        PhysicsWorld physicsWorld;
        // BodyHandle (raw id) -> owning entity index, for the current Play session's contact
        // events (app/physics_glue.h's DispatchContactEvents). Filled by BuildPhysicsWorld.
        std::unordered_map<uint32_t, int> bodyToEntity;
        AudioEngine audio;
        Scene scene;
        Camera camera;

        // Style shared by every object each frame: band count + ambient floor (a global shading
        // look). Outline color/width are per-object (Entity::material); outlineScale scales all
        // of their widths together.
        Material style;
        float outlineScale = 1.0f;
        PostParams post;

        // Gates UpdateScripts (core/scene/script.h) -- lets scripts be paused without stopping
        // the rest of the simulation. Read by the fixed-step loop (TickRuntime).
        bool runScripts = true;

        // World-space vertical sky gradient behind the scene (roadmap #12), drawn by RenderScene
        // into the HDR G-buffer as shared world content (not an editor-only overlay like the
        // grid, which stays on EditorState). Defaults match ToonEngineOld's grid.frag.
        bool showSky = true;
        Color skyTop{0.35f, 0.55f, 0.80f, 1.0f};
        Color skyBottom{0.15f, 0.15f, 0.20f, 1.0f};

        // Master volume + mute, applied via AudioEngine::SetMasterVolume (the settings menu,
        // roadmap #26, will own the player-facing control; today only the editor toggles them).
        float masterVolume = 1.0f;
        bool audioMuted = false;

        double lastTime = 0.0;
        // Fixed-timestep simulation clock (M1.1): gameplay state advances in fixed kFixedDt
        // steps, decoupled from the variable render rate, via this accumulator.
        double accumulator = 0.0;

        // Application lifecycle (roadmap #15, app/app_state.h). In the player, appState drives
        // the frame loop; in the editor it stays Playing and EditorMode decides sim ticking.
        AppState appState = AppState::Boot;
        // One-deep resume slot for Paused -> (whatever we paused from). Written by SetAppState.
        AppState resumeTo = AppState::Playing;
        // The current loading work list (drained while appState == Loading).
        LoadJob loadJob;
        // The scene the loader should bring up (seeded when entering Loading).
        std::string pendingScenePath;
    };

} // namespace toon
