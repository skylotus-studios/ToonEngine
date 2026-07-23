#pragma once
//============================================================================
//  app/editor_state.h: the editor's shared per-frame state.
//
//  A plain data bundle (no methods, no hidden internals) that main.cpp's InitEditor/
//  TickEditor/RenderFrame and every ui/panels/* function share by reference, the same
//  plain-struct-plus-free-functions shape core/scene/scene.h's Scene already uses.
//
//  Since roadmap #15 this is a THIN SHELL over RuntimeState (app/runtime_state.h): the engine
//  half (renderer, physics/audio worlds, scene, camera, render style, app lifecycle) lives in
//  the embedded `runtime`, and only the fields an editor adds on top -- panels, gizmo, themes,
//  the Play/Stop snapshot -- live here. Editor code reaches engine state through `state.runtime`
//  (e.g. state.runtime.scene); the player never constructs an EditorState at all.
//============================================================================
#include "app/runtime_state.h"
#include "core/rendering/renderer.h" // Camera (cameraDefault), Color
#include "core/scene/scene.h"        // Scene (sceneBackup)
#include "ui/panels/file_browser.h"
#include "ui/panels/themes.h"

#include "imgui.h" // ImDrawList etc. -- ImGuizmo.h assumes these are already declared
#include "ImGuizmo.h"

#include <string>

struct GLFWwindow;

namespace toon {

    // Editor vs. simulation state (M1.2): Editing poses the scene with nothing ticking;
    // Playing runs the fixed-timestep sim from TickEditor; Paused freezes it without
    // discarding progress. See ui/panels/playback_panel.cpp for the Play/Step/Stop transitions.
    // A DIFFERENT axis from RuntimeState::AppState (app/app_state.h): EditorMode gates whether
    // the editor's sim ticks; AppState is what an application is showing. In the editor AppState
    // stays Playing and EditorMode decides; the player has no EditorMode.
    enum class EditorMode { Editing, Playing, Paused };

    struct EditorState {
        // The engine half, shared with the player (app/runtime_state.h). Editor code reaches
        // renderer/scene/camera/etc. through this (state.runtime.scene, state.runtime.renderer).
        RuntimeState runtime;

        // Snapshot taken when Play starts, wholesale-restored on Stop (see playback_panel.cpp).
        // Only the scene is snapshotted (physics/audio worlds are rebuilt from it), so this stays
        // a Scene, not a whole RuntimeState.
        Scene sceneBackup;
        Camera cameraDefault; // for the "Reset camera" button
        // 2D editor mode (roadmap #14): the 3D yaw/pitch saved when entering 2D mode
        // (app/editor_tick.h's SetEditorMode2D), restored when leaving it, so toggling back
        // to 3D returns to the angle you left rather than resetting it. Defaults match
        // Camera's own yaw/pitch defaults, for a sensible value before 2D mode is ever entered.
        float saved3DYaw = 0.0f;
        float saved3DPitch = 0.25f;

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

        // Roadmap #10: last result of the Settings panel's "Reload Now" shader button
        // (Debug builds only -- an automatic file watcher already does this every frame; this
        // status just echoes the manual fallback's own last result, same shape as sceneStatus).
        std::string shaderReloadStatus;

        // "Contents" editor panel: browses assets/ with thumbnails.
        FileBrowser assetBrowser;

        // Editor-only render trailers (drawn by RenderFrame after RenderScene, never in the
        // player): the ground grid (roadmap #12, an authoring aid) and per-collider debug
        // wireframes (M2.1). The sky gradient is NOT here -- it's shared world content, so its
        // toggle + colors live on RuntimeState; the grid is editor-only, so its toggle is here.
        bool showGrid = true;
        bool showColliders = false;
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

        // Play/Pause/Step/Stop state (M1.2).
        EditorMode mode = EditorMode::Editing;
        bool stepRequested = false;
        bool suppressNextFrameHistory = false;
    };

} // namespace toon
