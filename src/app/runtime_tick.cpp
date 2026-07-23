//============================================================================
//  app/runtime_tick.cpp: see runtime_tick.h.
//============================================================================
#include "app/runtime_tick.h"

#include "app/physics_glue.h" // DispatchContactEvents
#include "core/audio/audio.h"
#include "core/camera/camera.h"
#include "core/input/input_system.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace toon {

    // Simulation rate: 60 Hz. The render rate is decoupled from this (see the accumulator).
    static constexpr double kFixedDt = 1.0 / 60.0;

    double RuntimeBeginFrame(RuntimeState &rs) {
        // BeginFrame BEFORE PollEvents: it snapshots previous state and clears this frame's
        // mouse/scroll deltas, so the callbacks PollEvents fires (key/mouse/scroll) accumulate
        // into a clean frame and WasPressed/WasReleased edge-detect correctly. See
        // core/input/input_system.h.
        Input::BeginFrame();
        glfwPollEvents();

        // Variable frame dt drives input-rate concerns (the editor's camera nav): it should feel
        // as smooth as the display, not snap to the sim's fixed rate. Clamped so a stall (a
        // breakpoint, or a window drag -- glfwPollEvents blocks for the duration on Windows, and
        // a synchronous scene load does the same) doesn't dump a large time debt into the
        // accumulator and trigger a many-step "spiral of death" catch-up burst; the sim just
        // falls a bit behind wall-clock instead.
        const double now = glfwGetTime();
        double frameTime = now - rs.lastTime;
        rs.lastTime = now;
        if (frameTime > 0.25) { frameTime = 0.25; }
        return frameTime;
    }

    void TickRuntime(RuntimeState &rs, double frameTime, const SimTickParams &p) {
        // Fixed-timestep simulation: advance in whole kFixedDt-sized steps regardless of the
        // variable frame rate, so gameplay state (script Update hooks; physics) evolves
        // deterministically. Usually one step per frame; zero if rendering outruns the sim
        // rate, several if the sim fell behind. Only advanceSim feeds the accumulator from
        // wall-clock; stepOnce (the editor's Step) credits exactly one kFixedDt so the SAME loop
        // drains exactly one iteration, no separate single-step path needed.
        const bool runFixedStepsThisFrame = p.advanceSim || p.stepOnce;
        if (p.advanceSim) { rs.accumulator += frameTime; }
        if (p.stepOnce) { rs.accumulator += kFixedDt; }
        if (runFixedStepsThisFrame) {
            while (rs.accumulator >= kFixedDt) {
                // Snapshot BEFORE integrating, so UpdateWorldTransforms below can interpolate the
                // render pose across the tick this step just produced.
                SnapshotSimState(rs.scene);

                // Run every entity's attached scripts for this tick (core/scene/script.h). Each
                // script advances its own entity's state incrementally, so a gizmo-set
                // orientation (set while paused) is the new baseline it continues from on resume.
                if (rs.runScripts) { UpdateScripts(rs.scene, static_cast<float>(kFixedDt)); }

                // Skeletal animation (roadmap #11): advance every playing clip by this tick.
                // Looping wraps at the clip's own duration; a non-looping clip clamps to its end.
                for (Entity &e : rs.scene.entities) {
                    if (!e.animation || !e.animation->playing || e.animation->clipIndex < 0) { continue; }
                    e.animation->time += static_cast<float>(kFixedDt);
                    const float duration = rs.renderer.GetModelAnimationDuration(
                        e.model, static_cast<uint32_t>(e.animation->clipIndex));
                    if (duration > 0.0f) {
                        if (e.animation->looping) {
                            e.animation->time = std::fmod(e.animation->time, duration);
                        } else {
                            e.animation->time = std::min(e.animation->time, duration);
                        }
                    }
                }

                // Physics (M2.1): push this tick's static/kinematic transforms into Jolt, step
                // once, then read every dynamic body back into its entity's transform via
                // ComposeWorldMatrix + SetEntityWorldMatrix (the same fold-out-the-parent path
                // the gizmo write-back uses).
                for (const Entity &e : rs.scene.entities) {
                    if (e.body && e.body->type != BodyType::Dynamic && e.transform) {
                        rs.physicsWorld.SetBodyTransform(e.body->handle, e.transform->position,
                                                         e.transform->rotation);
                    }
                }
                rs.physicsWorld.Step(static_cast<float>(kFixedDt));
                for (int i = 0; i < static_cast<int>(rs.scene.entities.size()); ++i) {
                    Entity &e = rs.scene.entities[i];
                    if (!e.body || e.body->type != BodyType::Dynamic || !e.transform) { continue; }
                    Vec3 bodyPos;
                    Quat bodyRot;
                    if (rs.physicsWorld.GetBodyTransform(e.body->handle, bodyPos, bodyRot)) {
                        const Mat4 world = ComposeWorldMatrix(bodyPos, bodyRot, e.transform->scale);
                        SetEntityWorldMatrix(rs.scene, i, world);
                    }
                }

                // Contact events (roadmap #9): dispatched after the write-back above, so a
                // script's OnCollision* sees this tick's already-updated post-physics transforms.
                if (rs.runScripts) { DispatchContactEvents(rs.physicsWorld, rs.scene, rs.bodyToEntity); }

                rs.accumulator -= kFixedDt;
            }
        }

        // Compose the hierarchy's world matrices (parents before children), rendering each
        // entity's pose interpolated between its previous and current sim tick by how far
        // `accumulator` has drifted into the next one -- smooth motion even when the display's
        // refresh rate doesn't match the fixed sim rate. Outside Playing, alpha is pinned to 1.0:
        // the accumulator isn't draining, so any leftover fraction is stale.
        const float alpha = p.advanceSim ? static_cast<float>(rs.accumulator / kFixedDt) : 1.0f;
        UpdateWorldTransforms(rs.scene, alpha);

        // View selection. The editor navved rs.camera before calling us; the player derives it
        // from the scene's primary CameraComponent here, AFTER world transforms are composed so
        // the camera entity's worldMatrix is current. If the scene has no primary camera,
        // GetActiveCamera leaves rs.camera untouched (the fallback the loader/default provides).
        if (p.cameraFromScene) { GetActiveCamera(rs.scene, rs.camera); }

        // Audio (M2.2): listener + emitter positions update on the RENDER frame, not the fixed
        // sim tick: audio is a presentation concern like rendering, so it tracks the same
        // smoothly-interpolated transforms just composed by UpdateWorldTransforms. The listener
        // follows the active view (rs.camera); emitters only move while the sim is advancing.
        {
            Vec3 eye, forward, up;
            CameraWorldBasis(rs.camera, eye, forward, up);
            rs.audio.SetListener(eye, forward, up);
        }
        if (p.advanceSim) {
            for (const Entity &e : rs.scene.entities) {
                if (e.audioSource && e.audioSource->spatial && e.audioSource->handle != SoundHandle::Invalid) {
                    rs.audio.SetPosition(e.audioSource->handle,
                                         {e.worldMatrix.m[12], e.worldMatrix.m[13], e.worldMatrix.m[14]});
                }
            }
        }

        // Push post params + camera + light: SetCamera reads post.taa to decide the TAA jitter,
        // and the shadow cascade pre-pass (RenderScene) needs the camera + light already set.
        rs.post.suppressTemporalHistory = p.suppressTemporalHistory;
        rs.renderer.SetPostParams(rs.post);
        rs.renderer.SetCamera(rs.camera);

        // Light: driven by the scene's first light entity (aimed via its rotation), falling back
        // to a fixed default if the scene has none (e.g. the user deleted "Sun").
        Vec3 lightDir{0.5f, 0.8f, -0.3f};
        Vec3 lightColor{1.0f, 1.0f, 1.0f};
        float lightIntensity = 1.0f;
        GetActiveLight(rs.scene, lightDir, lightColor, lightIntensity);
        rs.renderer.SetLight(lightDir, lightColor, lightIntensity);
    }

} // namespace toon
