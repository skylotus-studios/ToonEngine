#pragma once
//============================================================================
//  app/world_hash.h: a 64-bit fingerprint of everything the simulation just computed.
//
//  Verifying two full world saves are identical byte-for-byte means reading both start to
//  finish. Hashing is a checksum on a download: instead of re-reading the whole thing, boil it
//  down to one number, and if the numbers match, the contents matched (for every practical
//  purpose). Two --sim-only runs (app/sim_runtime.h) that print the same hash at tick 1200
//  simulated identically up to tick 1200, with neither run ever writing or comparing a save.
//
//  This is the app layer's glue, alongside app/physics_glue.h and app/audio_glue.h: it composes
//  the scene, physics, and script seams for one purpose (a state fingerprint), so it lives here
//  rather than inside any one of them.
//============================================================================
#include <cstdint>

namespace toon {

    struct RuntimeState;

    // Fingerprint the current simulated state: every entity's id (its vector index -- the same
    // identity convention Entity::parent/Scene::selected/ContactEvent's `other` already use, see
    // scene.h), its local transform, its physics body's authoritative pose + velocity, every
    // attached script's own Save()-serialized fields, and its animation clip index + time --
    // walked in scene order, which is already the hierarchy-defined order scene.h documents
    // (parents always precede children).
    //
    // Deliberately hashes Entity::transform (the local pose just written this tick), NOT
    // Entity::worldMatrix: TickRuntime's render-interpolation accumulator makes worldMatrix one
    // tick stale exactly when every tick is kFixedDt (as every --sim-only tick is) -- see
    // world_hash.cpp's own comment for the full reasoning.
    //
    // `seed` is folded into the hash as a domain separator (its bytes hashed before the first
    // entity), so two runs with different seeds always produce different streams even when
    // nothing simulated actually depends on the seed -- which is every system today; see
    // SimOptions::seed for why. This proves the seed REACHES the hash, not that any simulated
    // result is seed-sensitive.
    uint64_t HashWorldState(const RuntimeState &rs, unsigned long long seed);

    // A structural fingerprint of WHICH scripts are live and in WHAT ORDER, for
    // app/metrics.h's scripts.order_hash: walks the same scene order as HashWorldState, hashing
    // each live script's (entityIndex, scriptName) pair -- so a scene where the same scripts end
    // up attached to different entities, or in a different sequence, hashes differently even if
    // HashWorldState's own per-entity state still happened to match. No seed mixed in: this is a
    // pure structural fact about the loaded scene, not a per-run state snapshot.
    uint64_t HashScriptOrder(const RuntimeState &rs);

} // namespace toon
