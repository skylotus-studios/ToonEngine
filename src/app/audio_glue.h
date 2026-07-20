#pragma once
//============================================================================
//  app/audio_glue.h — audio-world (re)construction from AudioSource-bearing entities.
//
//  Twin to app/physics_glue.h's BuildPhysicsWorld -- called from the same Play/Step
//  transitions (ui/panels/playback_panel.cpp), right alongside it.
//============================================================================
#include "core/audio/audio.h"
#include "core/scene/scene.h"

namespace toon {

    // Start a handled sound for every AUTOPLAY AudioSource-bearing entity, seeded from its
    // current world position (spatial ones) and component fields. Called once whenever a
    // Play/Step session starts -- Stop just calls AudioEngine::StopAll(), no rebuild, since
    // the scene reverts to its pre-Play snapshot anyway (same shape as BuildPhysicsWorld).
    //
    // A non-autoplay emitter (e.g. one a script will trigger later) is skipped here; nothing
    // starts it yet -- script-triggered playback is out of scope for this pass (deferred
    // alongside M2.3's contact-events-to-scripts).
    //
    // Assumes every AudioSource-bearing entity is root-parented, same simplification
    // BuildPhysicsWorld already makes for colliders -- a nested emitter is seeded once here
    // but never correctly re-synced.
    void BuildAudioWorld(AudioEngine &audio, Scene &scene);

} // namespace toon
