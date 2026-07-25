//============================================================================
//  app/session.cpp: see session.h.
//============================================================================
#include "app/session.h"

#include "app/audio_glue.h"   // BuildAudioWorld
#include "app/physics_glue.h" // BuildPhysicsWorld
#include "app/runtime_state.h"
#include "core/scene/script.h"     // CreateScripts / DestroyScripts
#include "core/scene/serializer.h" // LoadSceneData

#include <GLFW/glfw3.h> // glfwGetTime (sim-clock reset across the swap)

#include <cstdio>
#include <utility>

namespace toon {
    namespace {

        // Half the transition's length: FadeOut then FadeIn each take this long. Long enough to
        // read as a deliberate transition rather than a dropped frame, short enough not to feel
        // like a wait on levels that currently load in milliseconds.
        constexpr float kFadeSeconds = 0.25f;

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        // Parse + upload the incoming level, then -- only if that succeeded -- tear the outgoing
        // one down and bring the new one up. Returns false with the current level untouched and
        // still playing if the load failed.
        bool SwapScene(RuntimeState &rs, const char *path) {
            // 1. Parse and upload into a side buffer. Nothing live is touched yet, so a failure
            //    here costs nothing: no body destroyed, no sound stopped, no entity dropped.
            Scene staging;
            Camera stagingCamera = rs.camera;
            if (!LoadSceneData(path, staging, stagingCamera, rs.renderer)) {
                std::fprintf(stderr, "Scene change aborted, staying in the current level: %s\n", path);
                return false;
            }

            // 2. The load succeeded, so the outgoing level is finally safe to destroy.
            EndSession(rs);

            // 3. Commit. pendingScenePath is what MakeSave records (app/save_glue.h), so a save
            //    taken after this names the level the player is actually standing in.
            rs.scene = std::move(staging);
            rs.camera = stagingCamera;
            rs.pendingScenePath = path;

            // 4. Bring the new level to life.
            BeginSession(rs);

            // 5. Reset the sim clock. The load spans wall-clock time the accumulator must never
            //    see: RuntimeBeginFrame's 0.25s clamp caps a single stall, but 0.25s is still 15
            //    fixed ticks -- a burst of catch-up steps that reads as a physics lurch on the
            //    new level's first frame. Same reasoning, same two lines, as SetAppState's
            //    Playing arm (app/app_state.cpp).
            rs.lastTime = glfwGetTime();
            rs.accumulator = 0.0;
            return true;
        }

    } // namespace

    // --- Session build / teardown -------------------------------------------------

    void BeginSession(RuntimeState &rs) {
        rs.scene.requestedScenePath.clear(); // a fresh session never inherits a pending request
        CreateScripts(rs.scene);                                        // fire OnCreate once
        BuildPhysicsWorld(rs.physicsWorld, rs.scene, rs.bodyToEntity);  // seed bodies from colliders
        BuildAudioWorld(rs.audio, rs.scene);                            // start autoplay emitters
    }

    void EndSession(RuntimeState &rs) {
        // 1. Scripts first, while every body and sound they might reach for on the way out is
        //    still valid. This is the hook's whole reason for existing.
        DestroyScripts(rs.scene);

        // 2. Audio next: stop the voices before anything they read can go away (miniaudio's own
        //    thread is what reads them), then drop the dead handles so a surviving entity -- the
        //    editor's Stop restores from a snapshot -- can't be mistaken for one still sounding.
        rs.audio.StopAll();
        for (Entity &e : rs.scene.entities) {
            if (e.audioSource) { e.audioSource->handle = SoundHandle::Invalid; }
        }

        // 3. Physics last: after this no contact callback can fire into a script that step 1 has
        //    already said goodbye to. Clear() is Jolt's RemoveBody-then-DestroyBody pair for
        //    every live body (core/physics/physics.cpp).
        rs.physicsWorld.Clear();
        rs.bodyToEntity.clear();
        for (Entity &e : rs.scene.entities) {
            if (e.body) { e.body->handle = BodyHandle::Invalid; }
        }

        // NOT released here: this scene's MeshHandles / ModelHandles / sprite TextureHandles.
        // See the header -- Renderer has no DestroyMesh/DestroyModel, Stop shares handles with
        // sceneBackup, and the refcounted resource manager that makes freeing them correct is
        // roadmap #20.
    }

    // --- Level transitions ---------------------------------------------------------

    void TickSceneTransition(RuntimeState &rs, double frameTime) {
        SceneTransition &tr = rs.transition;
        const float dt = static_cast<float>(frameTime);

        // A request only starts a transition when one isn't already running: a trigger the player
        // is still standing inside re-requests every tick, and that must not restart the fade it
        // already started.
        if (tr.phase == TransitionPhase::None && HasPendingSceneChange(rs.scene)) {
            tr.phase = TransitionPhase::FadeOut;
            tr.t = 0.0f;
            tr.targetScenePath = rs.scene.requestedScenePath;
            rs.scene.requestedScenePath.clear(); // consumed
        }

        // No default arm: adding a phase must be handled here explicitly (-Wswitch).
        switch (tr.phase) {
            case TransitionPhase::None:
                break;
            case TransitionPhase::FadeOut:
                tr.t += dt;
                if (tr.t >= kFadeSeconds) {
                    // Fully black -- the one moment in the frame where the scene vector can be
                    // replaced wholesale. Ignoring the result is deliberate: on failure
                    // SwapScene left the current level intact and logged why, and fading back
                    // into it beats stranding the player on a black screen.
                    SwapScene(rs, tr.targetScenePath.c_str());
                    tr.targetScenePath.clear();
                    tr.phase = TransitionPhase::FadeIn;
                    tr.t = 0.0f;
                }
                break;
            case TransitionPhase::FadeIn:
                tr.t += dt;
                if (tr.t >= kFadeSeconds) {
                    tr.phase = TransitionPhase::None;
                    tr.t = 0.0f;
                }
                break;
        }
    }

    void CancelSceneTransition(SceneTransition &tr) { tr = SceneTransition{}; }

    void BeginFadeIn(SceneTransition &tr) {
        tr.phase = TransitionPhase::FadeIn;
        tr.t = 0.0f;
        tr.targetScenePath.clear();
    }

    float SceneFadeLevel(const SceneTransition &tr) {
        switch (tr.phase) {
            case TransitionPhase::None:
                return 1.0f;
            case TransitionPhase::FadeOut:
                return Clamp01(1.0f - tr.t / kFadeSeconds);
            case TransitionPhase::FadeIn:
                return Clamp01(tr.t / kFadeSeconds);
        }
        return 1.0f; // unreachable: the switch above is exhaustive
    }

    bool SceneTransitionBlocksSim(const SceneTransition &tr) { return tr.phase == TransitionPhase::FadeOut; }

    bool SceneTransitionSuppressesHistory(const SceneTransition &tr) { return tr.phase != TransitionPhase::None; }

} // namespace toon
