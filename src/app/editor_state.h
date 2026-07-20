#pragma once
//============================================================================
//  app/editor_state.h — the editor's shared per-frame state.
//
//  A plain data bundle (no methods, no hidden internals) that main.cpp's InitEditor/
//  TickEditor/RenderFrame and every ui/panels/* function share by reference — the same
//  plain-struct-plus-free-functions shape core/scene/scene.h's Scene already uses, not a class
//  wrapping this state in private members:
//  nothing here hides a third-party dependency the way Renderer/PhysicsWorld's PIMPL does,
//  so there's nothing to justify hiding it behind accessors either.
//============================================================================
#include "core/audio/audio.h"
#include "core/camera/camera.h"
#include "core/physics/physics.h"
#include "core/rendering/renderer.h"
#include "core/scene/scene.h"
#include "ui/panels/file_browser.h"
#include "ui/panels/themes.h"

#include "imgui.h" // ImDrawList etc. -- ImGuizmo.h assumes these are already declared
#include "ImGuizmo.h"

#include <string>
#include <unordered_map>

struct GLFWwindow;

namespace toon {

    // Editor vs. simulation state (M1.2): Editing poses the scene with nothing ticking;
    // Playing runs the fixed-timestep sim from TickEditor; Paused freezes it without
    // discarding progress. See ui/panels/playback_panel.cpp for the Play/Step/Stop transitions.
    enum class EditorMode { Editing, Playing, Paused };

    struct EditorState {
        GLFWwindow *window = nullptr;

        Renderer renderer;
        PhysicsWorld physicsWorld;
        // BodyHandle (raw id) -> owning entity index, for this Play/Step session's contact
        // events (app/physics_glue.h's DispatchContactEvents). Filled by BuildPhysicsWorld,
        // cleared alongside physicsWorld.Clear() on Stop (see playback_panel.cpp).
        std::unordered_map<uint32_t, int> bodyToEntity;
        AudioEngine audio;
        Scene scene;
        // Snapshot taken when Play starts, wholesale-restored on Stop (see playback_panel.cpp).
        Scene sceneBackup;
        Camera camera;
        Camera cameraDefault; // for the "Reset camera" button

        // Style shared by every object each frame: band count + ambient floor (a global shading
        // look). Outline color/width are per-object (Entity::material), but this scales all of
        // their widths together.
        Material style;
        float outlineScale = 1.0f;
        PostParams post;

        Theme uiTheme = Theme::AmberYellow;
        float uiScale = 1.0f;
        float uiScaleY = 1.0f;

        // Transform-gizmo state (ImGuizmo): which handle is active + local/world space.
        ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE gizmoMode = ImGuizmo::WORLD;
        // Snapping: per-op step sizes. Snapping engages while the toggle is on OR Ctrl is held.
        bool gizmoSnap = false;
        float snapTranslate = 0.5f;  // world units
        float snapRotateDeg = 15.0f; // degrees
        float snapScale = 0.25f;     // scale factor

        // Scene serialization (core/scene/serializer.h) path field + status echoed in the
        // Settings panel (SaveScene/LoadScene also log to the console).
        char scenePathBuf[256] = {};
        std::string sceneStatus;

        // "Contents" editor panel: browses assets/ with thumbnails.
        FileBrowser assetBrowser;

        // Gates UpdateScripts (core/scene/script.h) -- lets scripts be paused without stopping
        // the rest of the simulation.
        bool runScripts = true;
        // M2.1: overlay each collider-bearing entity's shape as a wireframe (Settings panel).
        bool showColliders = false;
        // M2.2: master volume + mute (Settings panel) -- applied via AudioEngine::SetMasterVolume.
        float masterVolume = 1.0f;
        bool audioMuted = false;
        // The Properties panel's "Preview"/"Stop Preview" button (see properties_panel.cpp):
        // one global audition slot, tracked by handle + which entity started it, so switching
        // entities or pressing the button again always stops the right (or any) preview.
        SoundHandle previewHandle = SoundHandle::Invalid;
        int previewEntityIdx = -1;
#ifdef IMGUI_HAS_DOCK
        bool dockLayoutBuilt = false;
#endif

        // Which panels are open (View menu checkboxes, and each panel's own close button both
        // write these) and which modal the File/Help menus queued this frame.
        bool showHierarchy = true;
        bool showInspector = true;
        bool showDebug = true;
        bool showAssetBrowser = true;
        bool showPlayback = true;
        bool openScenePopupRequested = false;
        bool saveScenePopupRequested = false;
        bool aboutPopupRequested = false;

        double lastTime = 0.0;

        // Fixed-timestep simulation clock (M1.1): gameplay state advances in fixed kFixedDt
        // steps, decoupled from the variable render rate, via this accumulator.
        double accumulator = 0.0;

        // Play/Pause/Step/Stop state (M1.2).
        EditorMode mode = EditorMode::Editing;
        bool stepRequested = false;
        bool suppressNextFrameHistory = false;
    };

} // namespace toon
