//============================================================================
//  app/editor_tick.cpp: see editor_tick.h.
//
//  Since roadmap #15 this is a thin wrapper: the sim + presentation half lives in
//  app/runtime_tick.cpp (RuntimeBeginFrame + TickRuntime), shared with the player. TickEditor
//  adds only what the editor uniquely does -- gate input on ImGui capture and drive the
//  mouse/keyboard/gamepad editor camera -- between the frame prologue and the sim tick.
//============================================================================
#include "app/editor_tick.h"

#include "app/editor_state.h"
#include "app/runtime_tick.h"
#include "app/session.h" // TickSceneTransition + the sim/history gates (roadmap #19)
#include "core/camera/camera.h"
#include "core/input/action_map.h"
#include "core/input/input_system.h"

namespace toon {

    void TickEditor(EditorState &state) {
        RuntimeState &rt = state.runtime;

        // Frame prologue (input snapshot + event pump), shared with the player.
        const double frameTime = RuntimeBeginFrame(rt);
        const float dt = static_cast<float>(frameTime);

        // Gate the editor camera on ImGui capture OR an in-progress gizmo drag (both from last
        // frame's UI state, which is what's available before this frame's BeginUI).
        const ImGuiIO &io = ImGui::GetIO();
        const bool gizmoActive = ImGuizmo::IsUsing();
        Input::SetCaptured(io.WantCaptureMouse || gizmoActive, io.WantCaptureKeyboard);

        // Feeds PostParams::suppressTemporalHistory: an active gizmo drag, any ImGui widget being
        // edited, scripts continuously animating, or a Stop-restore/Step last frame (a pose jump,
        // not smooth motion) all mean post-fx temporal history shouldn't be trusted this frame.
        const bool suppressTemporalHistory =
            gizmoActive || ImGui::IsAnyItemActive() || rt.runScripts || state.suppressNextFrameHistory;
        state.suppressNextFrameHistory = false; // consumed -- only suppresses the one frame right after

        // Editor camera nav: right-drag orbits (+ WASD/QE fly); middle-drag pans; scroll zooms; F
        // focuses the origin. Orbit/fly are inert in 2D editor mode (roadmap #14), which locks the
        // view to a fixed angle facing the sprite plane; pan/zoom/focus are unaffected. Runs
        // before TickRuntime's SetCamera, so the navved rs.camera is what gets pushed this frame.
        {
            using M = Input::MouseButton;
            float mdx = 0.0f, mdy = 0.0f;
            Input::MouseDelta(mdx, mdy);
            if (!rt.camera.orthographic && Input::IsMouseDown(M::Right)) {
                CameraOrbit(rt.camera, -mdx, -mdy);
                // Fly axes go through the action map (camera.fly.*) so keyboard AND a gamepad
                // stick drive the same names. Guarded on WantCaptureKeyboard because GetAxis reads
                // raw device state (it bypasses SetCaptured); without the guard, typing in an
                // ImGui field while right-dragging would also fly the camera.
                if (!io.WantCaptureKeyboard) {
                    const float fwd = Input::GetAxis("camera.fly.forward");
                    const float rgt = Input::GetAxis("camera.fly.right");
                    const float upv = Input::GetAxis("camera.fly.up");
                    CameraFly(rt.camera, dt, fwd, rgt, upv);
                }
            }
            if (Input::IsMouseDown(M::Middle)) { CameraPan(rt.camera, mdx, mdy); }
            if (const float s = Input::ScrollDelta(); s != 0.0f) { CameraZoom(rt.camera, s); }
            if (!io.WantCaptureKeyboard && Input::WasActionPressed("camera.focus")) {
                CameraFocus(rt.camera, {0.0f, 0.0f, 0.0f});
            }

            // Gamepad orbit (right stick): scaled by dt so the turn rate is frame-rate
            // independent, unlike the per-frame pixel deltas a mouse drag feeds CameraOrbit.
            // Gated on orthographic for the same reason the mouse-driven orbit above is.
            const float gpOrbitX = Input::GetAxis("camera.orbit.x");
            const float gpOrbitY = Input::GetAxis("camera.orbit.y");
            if (!rt.camera.orthographic && (gpOrbitX != 0.0f || gpOrbitY != 0.0f)) {
                constexpr float kGamepadOrbitRate = 150.0f; // pixel-equivalents/sec at full deflection
                CameraOrbit(rt.camera, gpOrbitX * kGamepadOrbitRate * dt, -gpOrbitY * kGamepadOrbitRate * dt);
            }
        }

        // Level transitions (roadmap #19), on the same code path the player uses -- testing one
        // shouldn't mean building and launching ToonPlayer. Skipped in Editing: only a ticking
        // script can request a transition, and a scene the user is authoring must never be
        // swapped out from under them. Runs before TickRuntime so this frame's post params carry
        // the fade level it just advanced.
        if (state.mode != EditorMode::Editing) { TickSceneTransition(rt, frameTime); }

        // Sim + presentation. The editor gates the sim through its own EditorMode (Playing feeds
        // wall-clock; Step credits exactly one fixed step); rs.camera was just navved above, so
        // TickRuntime uses it as-is (cameraFromScene stays false -- that's the player's path).
        SimTickParams sim;
        sim.advanceSim = (state.mode == EditorMode::Playing) && !SceneTransitionBlocksSim(rt.transition);
        sim.stepOnce = state.stepRequested;
        sim.suppressTemporalHistory = suppressTemporalHistory || SceneTransitionSuppressesHistory(rt.transition);
        state.stepRequested = false; // consumed
        TickRuntime(rt, frameTime, sim);
    }

    void SetEditorMode2D(EditorState &state, bool on2D) {
        Camera &cam = state.runtime.camera;
        if (on2D && !cam.orthographic) {
            state.saved3DYaw = cam.yaw;
            state.saved3DPitch = cam.pitch;
            // Face the sprite plane's front (eye on the +Z side, looking toward -Z): sprite.hlsl's
            // quad is wound CCW as seen from +Z, and CameraBasis's forward points from eye toward
            // the pivot, so yaw = pi (pitch 0) shows sprites right-side-up rather than mirrored.
            constexpr float kPi = 3.14159265f;
            cam.yaw = kPi;
            cam.pitch = 0.0f;
            cam.orthographic = true;
        } else if (!on2D && cam.orthographic) {
            cam.yaw = state.saved3DYaw;
            cam.pitch = state.saved3DPitch;
            cam.orthographic = false;
        }
    }

} // namespace toon
