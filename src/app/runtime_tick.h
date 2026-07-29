#pragma once
//============================================================================
//  app/runtime_tick.h: the engine-half per-frame tick, shared by the editor and the player.
//
//  This is the sim + presentation half of what used to be one monolithic TickEditor: the
//  fixed-timestep loop (scripts, animation, physics, contact events), render interpolation,
//  the audio listener/emitters, and the per-frame render-state push (camera/post/light). It
//  takes a RuntimeState&, never an EditorState&, so the player can drive it with no ImGui and
//  no ui/panels/. The editor's TickEditor (app/editor_tick.h) wraps these plus its camera nav.
//============================================================================
#include "app/runtime_state.h"

namespace toon {

    // Simulation rate: 60 Hz. The render rate is decoupled from this (see TickRuntime's
    // accumulator). Lives in the header rather than file-local to runtime_tick.cpp because
    // app/sim_runtime.cpp drives the same loop with no wall clock at all, feeding this value
    // directly as the frame delta -- one definition, so the two can never drift apart.
    constexpr double kFixedDt = 1.0 / 60.0;

    // What the sim should do this frame. The two callers differ only in how they fill this:
    // the editor from its EditorMode/Step state, the player from its AppState.
    struct SimTickParams {
        bool advanceSim = false;              // feed the accumulator from wall-clock time (Playing)
        bool stepOnce = false;                // credit exactly one fixed step (editor Step); never in the player
        bool cameraFromScene = false;         // derive the view from the scene's primary CameraComponent (player); the editor sets rs.camera itself (nav)
        bool suppressTemporalHistory = false; // written into rs.post; the editor computes it from gizmo/UI activity, the player leaves it false
    };

    // Frame prologue: snapshot input, pump window events, and return this frame's clamped
    // wall-clock delta (seconds). Updates rs.lastTime. Both callers run this first; the editor
    // then does its camera nav with the returned dt before calling TickRuntime.
    double RuntimeBeginFrame(RuntimeState &rs);

    // Advance the fixed-timestep sim by `frameTime` per `p`, interpolate the render pose, place
    // the audio listener + emitters, and push camera/post/light to the renderer. rs.camera must
    // already hold the intended view (the editor navved it; or set p.cameraFromScene to derive
    // it from the scene here, after world transforms are composed).
    void TickRuntime(RuntimeState &rs, double frameTime, const SimTickParams &p);

} // namespace toon
