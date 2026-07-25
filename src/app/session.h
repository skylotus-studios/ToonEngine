#pragma once
//============================================================================
//  app/session.h: the lifetime of a live scene, and moving between them (roadmap #19).
//
//  A "session" is one scene brought to life: its scripts created, its Jolt bodies built, its
//  autoplay emitters started. Both the editor's Play button and the standalone player's runtime
//  need exactly that pair of operations, and before this module they existed only as loose
//  statement sequences inside ui/panels/playback_panel.cpp -- an editor-only TU ToonPlayer does
//  not link, which is why a shipped player ran no scripts, created no physics bodies, and
//  started no audio. Extracting them here is what makes a level transition expressible at all
//  (a transition IS EndSession + load + BeginSession) and fixes the player as a side effect.
//
//  Runtime-side: takes RuntimeState&, never EditorState&, and includes no ImGui, so this TU
//  lives in ToonRuntime and both executables get it.
//============================================================================
#include <string>

namespace toon {

    struct RuntimeState; // app/runtime_state.h (holds the SceneTransition below)

    // --- Session build / teardown -------------------------------------------------

    // Bring the currently-loaded scene to life: fire every script's OnCreate, build the physics
    // world from the collider-bearing entities, start every autoplay emitter. Call once when a
    // session begins -- the editor's Play/Step, the runtime's first load, or the far side of a
    // level transition. Clears any pending scene-change request: a fresh session never inherits
    // one from whatever ran before it.
    void BeginSession(RuntimeState &rs);

    // Release everything the live session owns, in dependency order:
    //
    //   1. Scripts' OnDestroy, FIRST -- while the bodies and sound handles a script might want
    //      to touch on the way out are all still valid. Nothing else in this engine ever fired
    //      that hook before roadmap #19.
    //   2. Audio: StopAll + clear every AudioSource::handle. miniaudio owns a real OS audio
    //      thread (core/audio/audio.cpp's banner), so the voices have to stop before anything
    //      they read goes away.
    //   3. Physics: PhysicsWorld::Clear (Jolt's RemoveBody then DestroyBody) + the
    //      body-to-entity map + every RigidBodyComponent::handle. Last, because a contact
    //      callback firing into a half-destroyed script is exactly the hazard step 1 avoids.
    //
    // Deliberately leaves scene.entities ALONE: the caller decides whether the scene is being
    // replaced (a level transition) or restored from a snapshot (the editor's Stop), and this
    // function serves both.
    //
    // Also deliberately does NOT release this scene's MeshHandles / ModelHandles / sprite
    // TextureHandles. Renderer has no DestroyMesh or DestroyModel, and the editor's Stop path
    // shares handles with its sceneBackup, so a naive free here would double-free on revert.
    // A level transition therefore leaks the outgoing level's GPU memory, bounded by (levels
    // visited x level size) per run. The load-path cache and reference counting that make this
    // correct are roadmap #20, which is scoped to replace any half-measure built now.
    void EndSession(RuntimeState &rs);

    // --- Level transitions ---------------------------------------------------------

    // No Swap phase: the swap is instantaneous, performed at the moment FadeOut completes.
    enum class TransitionPhase { None, FadeOut, FadeIn };

    // One in-flight level change. Lives on RuntimeState; driven by TickSceneTransition below.
    //
    // Kept OFF AppState on purpose. That enum is deliberately flat and small (see
    // app/app_state.h), and its states answer "what is the application showing"; fade phases are
    // sub-states of one transition, not peers of Playing. Keeping them here also means the
    // EDITOR gets transitions for free -- it never sets AppState at all, but it does own a
    // RuntimeState.
    struct SceneTransition {
        TransitionPhase phase = TransitionPhase::None;
        float t = 0.0f;               // seconds elapsed in the current phase
        std::string targetScenePath;  // captured when FadeOut began; loaded when it completes
    };

    // Advance the current transition, if any, by `frameTime`, and start one if a script has
    // requested a scene change (core/scene/scene.h's RequestSceneChange). Call once per frame
    // from the frame loop, AFTER the frame prologue and BEFORE TickRuntime, so the fade level
    // this produces is the one TickRuntime pushes with the frame's post params.
    //
    // This is the "known-safe point" the whole design turns on: it runs outside the fixed-step
    // simulation loop and outside any script, physics, or audio callback, so the scene vector
    // can be replaced wholesale here and nowhere else.
    //
    // The swap order is what makes a failed load harmless: the incoming level is parsed and
    // uploaded into a side buffer FIRST, and only once that succeeds is the outgoing level torn
    // down. A bad path logs, reverses the fade, and leaves the current level playing.
    void TickSceneTransition(RuntimeState &rs, double frameTime);

    // Abandon an in-flight transition and snap back to full brightness. The editor's Stop needs
    // this: it restores the pre-Play scene snapshot, so a FadeOut still counting down would
    // otherwise complete afterward and load the requested level over the scene the user is now
    // authoring again.
    void CancelSceneTransition(SceneTransition &tr);

    // Start a fade-in with no preceding fade-out, for the first level a run brings up (the boot
    // load completing). The game appears rather than cutting in hard, and the first frame after
    // a load -- the one most likely to be uneven -- is hidden behind it.
    void BeginFadeIn(SceneTransition &tr);

    // 1.0 normally, ramping to 0.0 across FadeOut and back up across FadeIn. Applied by
    // TickRuntime as a multiplier onto a COPY of PostParams::exposure, so the fade rides the
    // tone-map resolve that already ships (assets/shaders/tonemap.hlsl's `hdr *= g_Exposure`)
    // instead of needing an overlay draw, a blend pipeline, or a new shader -- and the user's
    // authored exposure value is never overwritten.
    float SceneFadeLevel(const SceneTransition &tr);

    // True while the outgoing level is fading out: the caller must not advance the simulation.
    // False during FadeIn, where the new level is live and should be moving as it appears.
    bool SceneTransitionBlocksSim(const SceneTransition &tr);

    // True for the whole transition. Post-fx temporal history (TAA, SSAO accumulation) from the
    // previous level is meaningless in the new one, the same reason the editor's Stop-restore
    // sets suppressNextFrameHistory. Suppressing across the entire fade rather than for a single
    // frame costs nothing visible: the screen is at or near black for most of it.
    bool SceneTransitionSuppressesHistory(const SceneTransition &tr);

} // namespace toon
