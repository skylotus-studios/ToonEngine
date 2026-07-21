#pragma once
//============================================================================
//  core/physics.h: ToonEngine's physics seam.
//
//  Twin to core/renderer.h: the ONE header the rest of the engine talks to for physics
//  work. Every Jolt header and JPH:: type lives behind it in physics.cpp via PIMPL, so no
//  Jolt type, and no Jolt header, escapes into engine or game code (per the project's
//  build-on-Diligent guiding principle and the renderer seam it already established). This
//  header speaks
//  only toon:: types: Vec3, Quat (core/math.h), and plain enums/structs defined below.
//============================================================================
#include "core/math.h" // toon::Vec3/Quat (plain, Jolt- and Diligent-free)

#include <cstdint>
#include <vector>

namespace toon {

// --- Opaque handle -----------------------------------------------------------
// One physics body, named the same opaque-32-bit-id way renderer.h names GPU resources
// (MeshHandle, ModelHandle, ...). The id-to-Jolt-BodyID mapping lives entirely inside
// PhysicsWorld::Impl; nothing outside physics.cpp ever sees a JPH::BodyID.
enum class BodyHandle : uint32_t { Invalid = 0 };

// --- Scene vocabulary ---------------------------------------------------------

enum class ColliderShape { Box, Sphere, Capsule };

// Static: never moves, infinite effective mass (the ground/walls); cheapest, since the
// solver never has to push it. Dynamic: fully simulated, gravity and collisions move it.
// Kinematic: moved by app code (SetBodyTransform each tick), never moved BY the sim, but
// still shoves dynamic bodies aside (a moving platform).
enum class BodyType { Static, Dynamic, Kinematic };

// Everything CreateBody needs to seed one body. `extents` means different things per
// shape (like PrimitiveDesc's kind-dependent fields, core/primitives.h: avoids three
// near-identical structs for three shapes): Box -> half-extents (x, y, z); Sphere ->
// radius in .x (y/z unused); Capsule -> half-height-of-cylinder in .x, radius in .y
// (z unused).
struct BodyDesc {
    ColliderShape shape = ColliderShape::Box;
    Vec3          extents = {0.5f, 0.5f, 0.5f};
    BodyType      type = BodyType::Dynamic;
    float         mass = 1.0f; // ignored for Static/Kinematic
    float         friction = 0.5f;
    float         restitution = 0.2f;
    Vec3          position = {0.0f, 0.0f, 0.0f};
    Quat          rotation; // identity by default
};

struct RaycastHit {
    BodyHandle body = BodyHandle::Invalid;
    Vec3       point;
    Vec3       normal;
    float      distance = 0.0f;
};

// Enter: the tick two bodies started touching. Stay: they're still touching this tick (fires
// every tick while resting against each other). Exit: they stopped touching. Named to match
// Unity's OnCollisionEnter/Stay/Exit, the same naming precedent this codebase already follows
// for ScriptComponent/NativeScriptComponent.
enum class ContactPhase { Enter, Stay, Exit };

// One contact between two bodies, queued during PhysicsWorld::Step and drained by
// ConsumeContactEvents. `point`/`normal` describe one representative contact position on the
// touching surface (a real contact can have several; this seam reports just one, enough for a
// script deciding where/which way to react). `normal` points from `a`'s side toward `b`'s
// side. On Exit, Jolt no longer has live contact geometry to report (the bodies may already be
// gone), so `point`/`normal` are the last values seen for this pair on its most recent
// Enter/Stay.
struct ContactEvent {
    BodyHandle    a = BodyHandle::Invalid;
    BodyHandle    b = BodyHandle::Invalid;
    ContactPhase  phase = ContactPhase::Enter;
    Vec3          point;
    Vec3          normal;
};

// --- PhysicsWorld --------------------------------------------------------------
// PIMPL, the same rationale as Renderer (core/renderer.h): Jolt already provides its own
// internal dispatch, so a second virtual-dispatch layer here would buy nothing. See
// renderer.h's own "why PIMPL, not a virtual IRenderer" precedent (docs/architecture.md).
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld &) = delete;
    PhysicsWorld &operator=(const PhysicsWorld &) = delete;

    // One-time setup: Jolt's process-global allocator/factory/type registry, the temp
    // allocator + job system, the broad/object-layer filters, PhysicsSystem::Init. Call
    // once (after the window/renderer exist); Shutdown once at exit.
    bool Init();
    void Shutdown();

    // Remove + destroy every live body, e.g. on Stop, or rebuilding the world for a new
    // Play session. Mirrors Scene's DestroyScene: cheap, since nothing outside this class
    // ever holds a Jolt resource directly.
    void Clear();

    // Add one body seeded from `desc`. Returns BodyHandle::Invalid on failure (e.g. out of
    // the fixed body budget: logs to stderr).
    BodyHandle CreateBody(const BodyDesc &desc);
    void       DestroyBody(BodyHandle body);

    // Push an authoritative transform onto a body: a static body's placement, or a
    // kinematic body's per-tick target (see BodyType's comment).
    void SetBodyTransform(BodyHandle body, const Vec3 &position, const Quat &rotation);

    // Read a dynamic body's simulated pose back out, for writing into an entity's
    // Transform. Returns false (out-params untouched) for an invalid/unknown handle.
    bool GetBodyTransform(BodyHandle body, Vec3 &outPosition, Quat &outRotation) const;

    // Advance the simulation by exactly `dt` (one collision step); call once per fixed
    // sim tick, at Jolt's own recommended fixed rate (1/60 s).
    void Step(float dt);

    void SetGravity(const Vec3 &gravity);

    // Closest-hit raycast against every live body. `direction` carries both the ray's
    // direction AND length (a hit beyond that length is not reported), Jolt's own
    // RayCast convention. Returns false (outHit untouched) on a miss. Ships with this seam
    // now so a future gameplay raycast has no new plumbing to add; nothing calls it yet
    // (the shipped mouse-pick uses a geometric ray-vs-bounds test instead, not this).
    bool Raycast(const Vec3 &origin, const Vec3 &direction, RaycastHit &outHit) const;

    // Drain every contact event queued since the last call (Enter/Stay on every touching pair,
    // Exit on every pair that stopped touching). Call once per fixed tick, right after Step():
    // events are collected on Jolt's own physics worker threads during Step (see physics.cpp),
    // and only safe to read from the caller's thread once Step has returned. Clears the queue.
    std::vector<ContactEvent> ConsumeContactEvents();

private:
    struct Impl; // defined in physics.cpp; hides all Jolt types
    Impl *m_impl = nullptr;
};

// Local-space line-segment endpoints (consecutive pairs: [0]-[1], [2]-[3], ...) outlining
// `shape`'s wireframe: pure geometry, no Jolt dependency, so the collider-debug-overlay
// draw path (a future core/renderer.h DrawWireframe call) can stay Diligent- AND
// Jolt-free. Declared here since it shares BodyDesc's shape vocabulary; implemented in
// Phase F (M2.1's collider-wireframe item), the first and only caller.
std::vector<Vec3> ColliderWireframe(ColliderShape shape, const Vec3 &extents);

} // namespace toon
