//============================================================================
//  app/editor_tick.cpp: see editor_tick.h.
//============================================================================
#include "app/editor_tick.h"

#include "app/editor_state.h"
#include "app/physics_glue.h" // DispatchContactEvents
#include "core/audio/audio.h"
#include "core/camera/camera.h"
#include "core/input/action_map.h"
#include "core/input/input_system.h"

#include <GLFW/glfw3.h>

namespace toon {

    void TickEditor(EditorState &state) {
        // BeginFrame BEFORE PollEvents: it snapshots previous state and clears this frame's
        // mouse/scroll deltas, so the callbacks PollEvents fires (key/mouse/scroll) accumulate
        // into a clean frame and WasPressed/WasReleased edge-detect correctly. See
        // core/input/input_system.h.
        Input::BeginFrame();
        glfwPollEvents();

        // Variable frame dt drives input-rate concerns (camera nav below): it should feel as
        // smooth as the display, not snap to the sim's fixed rate. Clamped so a stall (a
        // breakpoint, or a window drag -- glfwPollEvents blocks for the duration on Windows)
        // doesn't dump a large time debt into the accumulator below and trigger a many-step
        // "spiral of death" catch-up burst; the sim just falls a bit behind wall-clock instead.
        const double now = glfwGetTime();
        double frameTime = now - state.lastTime;
        state.lastTime = now;
        if (frameTime > 0.25) { frameTime = 0.25; }
        const float dt = static_cast<float>(frameTime);

        // Fixed-timestep simulation: advance in whole kFixedDt-sized steps regardless of the
        // variable frame rate above, so gameplay state (script Update hooks; physics) evolves
        // deterministically. Usually one step per frame; zero if rendering outruns the sim
        // rate, several if the sim fell behind.
        // M1.2: only Playing feeds the accumulator from wall-clock time -- Editing/Paused freeze
        // it so no time debt piles up while stopped. Step (from the Playback panel) credits it
        // with exactly one kFixedDt instead, so the SAME while loop below drains exactly one
        // iteration, no separate single-step code path needed.
        constexpr double kFixedDt = 1.0 / 60.0; // simulation rate: 60 Hz
        const bool runFixedStepsThisFrame = (state.mode == EditorMode::Playing) || state.stepRequested;
        if (state.mode == EditorMode::Playing) { state.accumulator += frameTime; }
        if (state.stepRequested) {
            state.accumulator += kFixedDt;
            state.stepRequested = false; // consumed
        }
        if (runFixedStepsThisFrame) {
            while (state.accumulator >= kFixedDt) {
                // Snapshot BEFORE integrating, so UpdateWorldTransforms below can interpolate the
                // render pose across the tick this step just produced.
                SnapshotSimState(state.scene);

                // Run every entity's attached scripts for this tick (core/scene/script.h) -- e.g.
                // the Sphere/Cube/Torus/Helmet's SpinScript. Each script advances its own entity's
                // state incrementally (SpinScript pre-multiplies a small delta rotation onto
                // whatever `rotation` currently is), so a gizmo-set orientation (set while paused)
                // is the new baseline it continues from on resume, instead of snapping back to
                // where an absolute clock-based formula would say it "should" be.
                if (state.runScripts) { UpdateScripts(state.scene, static_cast<float>(kFixedDt)); }

                // Physics (M2.1): push this tick's static/kinematic transforms into Jolt (so a
                // gizmo-dragged wall, say, is reflected before the step that would otherwise
                // ignore it), step once, then read every dynamic body back into its entity's
                // transform via ComposeWorldMatrix + SetEntityWorldMatrix (the same fold-out-
                // the-parent path the gizmo write-back uses). No separate "run physics" toggle,
                // unlike scripts' Run Scripts checkbox -- nothing in the roadmap called for one.
                for (const Entity &e : state.scene.entities) {
                    if (e.body && e.body->type != BodyType::Dynamic && e.transform) {
                        state.physicsWorld.SetBodyTransform(e.body->handle, e.transform->position,
                                                            e.transform->rotation);
                    }
                }
                state.physicsWorld.Step(static_cast<float>(kFixedDt));
                for (int i = 0; i < static_cast<int>(state.scene.entities.size()); ++i) {
                    Entity &e = state.scene.entities[i];
                    if (!e.body || e.body->type != BodyType::Dynamic || !e.transform) { continue; }
                    Vec3 bodyPos;
                    Quat bodyRot;
                    if (state.physicsWorld.GetBodyTransform(e.body->handle, bodyPos, bodyRot)) {
                        const Mat4 world = ComposeWorldMatrix(bodyPos, bodyRot, e.transform->scale);
                        SetEntityWorldMatrix(state.scene, i, world);
                    }
                }

                // Contact events (roadmap #9): dispatched after the write-back above, so a
                // script's OnCollision* sees this tick's already-updated post-physics
                // transforms, not last tick's. Gated on runScripts like UpdateScripts above --
                // no point draining events nothing will react to.
                if (state.runScripts) { DispatchContactEvents(state.physicsWorld, state.scene, state.bodyToEntity); }

                state.accumulator -= kFixedDt;
            }
        }

        // Compose the hierarchy's world matrices (parents before children), rendering each
        // entity's pose interpolated between its previous and current sim tick by how far
        // `accumulator` has drifted into the next one -- smooth motion even when the display's
        // refresh rate doesn't match the fixed sim rate. Motion vectors come from the cached
        // previous world matrices (see UpdateWorldTransforms), so no separate prev-angle
        // bookkeeping is needed here. Outside Playing (Editing/Paused), alpha is pinned to 1.0
        // -- accumulator isn't draining, so any interpolation fraction left over from the last
        // Play session is stale; rendering the exact current tick avoids blending a paused/
        // edited pose against that stale leftover.
        const float alpha =
            (state.mode == EditorMode::Playing) ? static_cast<float>(state.accumulator / kFixedDt) : 1.0f;
        UpdateWorldTransforms(state.scene, alpha);

        // Audio (M2.2): listener + emitter positions update on the RENDER frame, not the fixed
        // sim tick above: audio is a presentation concern like rendering, not a determinism
        // one, so it should track the same smoothly-interpolated transforms just composed by
        // UpdateWorldTransforms rather than the raw stepped sim pose (see core/audio/audio.h's
        // SetListener comment). Runs every frame regardless of mode, harmless while Editing/
        // Paused (BuildAudioWorld hasn't started anything yet, or PauseAll froze it), and keeps
        // the listener already correctly placed the instant Play starts.
        {
            Vec3 eye, forward, up;
            CameraWorldBasis(state.camera, eye, forward, up);
            state.audio.SetListener(eye, forward, up);
        }
        if (state.mode == EditorMode::Playing) {
            for (const Entity &e : state.scene.entities) {
                if (e.audioSource && e.audioSource->spatial && e.audioSource->handle != SoundHandle::Invalid) {
                    state.audio.SetPosition(e.audioSource->handle,
                                            {e.worldMatrix.m[12], e.worldMatrix.m[13], e.worldMatrix.m[14]});
                }
            }
        }

        // Editor camera: poll input, gate on ImGui's capture (last frame's UI state), then
        // navigate. Right-drag orbits (+ WASD/QE = fly); middle-drag pans; scroll zooms;
        // F focuses the origin. Dragging over the debug panel is suppressed by the gate.
        const ImGuiIO &io = ImGui::GetIO();
        // Gate the camera on ImGui capture OR an in-progress gizmo drag (both from last frame).
        const bool gizmoActive = ImGuizmo::IsUsing();
        Input::SetCaptured(io.WantCaptureMouse || gizmoActive, io.WantCaptureKeyboard);
        // Feeds PostParams::suppressTemporalHistory (see its comment): an active gizmo drag, any
        // ImGui widget being edited, scripts continuously animating, or a Stop-restore/Step from
        // the Playback panel last frame (a pose jump, not smooth motion) all mean post-fx
        // temporal history shouldn't be trusted this frame.
        const bool suppressTemporalHistory =
            gizmoActive || ImGui::IsAnyItemActive() || state.runScripts || state.suppressNextFrameHistory;
        state.suppressNextFrameHistory = false; // consumed -- only suppresses the one frame right after
        {
            using M = Input::MouseButton;
            float mdx = 0.0f, mdy = 0.0f;
            Input::MouseDelta(mdx, mdy);
            if (Input::IsMouseDown(M::Right)) {
                CameraOrbit(state.camera, -mdx, -mdy);
                // Fly axes go through the action map (camera.fly.*) so keyboard AND a gamepad
                // stick drive the same names; see action_map.cpp's RegisterDefaultEditorBindings.
                // Guarded on WantCaptureKeyboard because GetAxis reads raw device state (it
                // bypasses SetCaptured, like the rest of the action-map layer). Without the
                // guard, typing in an ImGui field while right-dragging would also fly the camera.
                if (!io.WantCaptureKeyboard) {
                    const float fwd = Input::GetAxis("camera.fly.forward");
                    const float rgt = Input::GetAxis("camera.fly.right");
                    const float upv = Input::GetAxis("camera.fly.up");
                    CameraFly(state.camera, dt, fwd, rgt, upv);
                }
            }
            if (Input::IsMouseDown(M::Middle)) { CameraPan(state.camera, mdx, mdy); }
            if (const float s = Input::ScrollDelta(); s != 0.0f) { CameraZoom(state.camera, s); }
            if (!io.WantCaptureKeyboard && Input::WasActionPressed("camera.focus")) {
                CameraFocus(state.camera, {0.0f, 0.0f, 0.0f});
            }

            // Gamepad orbit (right stick): a new capability the action map adds; ungated (unlike
            // the keyboard-sourced queries above) since a physical stick is never ambiguous with
            // ImGui text entry. Scaled by dt so the turn rate is frame-rate independent, unlike the
            // per-frame pixel deltas CameraOrbit otherwise expects from a mouse drag.
            const float gpOrbitX = Input::GetAxis("camera.orbit.x");
            const float gpOrbitY = Input::GetAxis("camera.orbit.y");
            if (gpOrbitX != 0.0f || gpOrbitY != 0.0f) {
                // Pixel-equivalents/sec at full stick deflection. An untested starting point: no
                // controller in this environment to feel-tune it against (see the verify skill);
                // adjust if a full stick push turns too fast or too slow.
                constexpr float kGamepadOrbitRate = 150.0f;
                CameraOrbit(state.camera, gpOrbitX * kGamepadOrbitRate * dt, -gpOrbitY * kGamepadOrbitRate * dt);
            }
        }

        // Post params + camera + light up front: SetCamera reads post.taa to decide the TAA
        // jitter, and the shadow cascade pre-pass (RenderFrame) needs the camera + light already
        // set -- it renders into its own depth-only targets, so it must run before BeginFrame
        // binds the main G-buffer.
        state.post.suppressTemporalHistory = suppressTemporalHistory;
        state.renderer.SetPostParams(state.post);
        state.renderer.SetCamera(state.camera);

        // Light: driven by the scene's first light entity (aimed via its rotation), falling back
        // to this fixed default if the scene has none (e.g. the user deleted "Sun").
        Vec3 lightDir{0.5f, 0.8f, -0.3f};
        Vec3 lightColor{1.0f, 1.0f, 1.0f};
        float lightIntensity = 1.0f;
        GetActiveLight(state.scene, lightDir, lightColor, lightIntensity);
        state.renderer.SetLight(lightDir, lightColor, lightIntensity);
    }

} // namespace toon
