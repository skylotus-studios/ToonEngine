#pragma once
//============================================================================
//  app/physics_glue.h — collider-extent scaling + physics-world (re)construction.
//
//  Shared by editor_tick.cpp (rebuilding the world when Play/Step starts) and
//  ui/panels/settings_panel.cpp's collider-wireframe overlay, which mirrors
//  BuildPhysicsWorld's own extent scaling so the debug overlay always shows what Jolt is
//  actually simulating.
//============================================================================
#include "core/physics/physics.h"
#include "core/scene/scene.h"

#include <string>
#include <unordered_map>

namespace toon {

    // A Box collider's half-extents are already 3 independent values, so a non-uniform
    // scale bakes in cleanly, one axis at a time. Sphere/Capsule only have 1-2 degrees of
    // freedom (a radius, a half-height), so a non-uniform scale there has no exact
    // representation as a plain SphereShape/CapsuleShape -- approximate with the largest
    // relevant axis and say so, rather than silently picking one. Exact ellipsoid/deformed-
    // capsule shapes are out of scope for M2.1's Box/Sphere/Capsule set.
    // logWarnings defaults on for the once-per-Play BuildPhysicsWorld call site; the
    // per-frame collider-wireframe overlay passes false so a misconfigured entity doesn't
    // spam stderr 60+ times a second on top of the warning Play already gave it.
    Vec3 ScaledColliderExtents(const ColliderComponent &collider, const Vec3 &scale, const std::string &entityName,
                               bool logWarnings = true);

    // Rebuild the physics world from the scene's current collider-bearing entities. Called
    // once whenever a Play/Step session starts -- Stop just Clear()s, no rebuild, since the
    // scene reverts to its pre-Play snapshot anyway.
    //
    // Assumes every collider-bearing entity is root-parented, so its local `transform` IS
    // its world pose: a real hierarchy fold (parent's world * local) is deliberately out of
    // scope for M2.1 -- a collider on a nested entity is seeded once here but never
    // correctly re-synced.
    //
    // outBodyToEntity: cleared, then filled with each new body's BodyHandle (as its raw id) ->
    // owning entity index, for DispatchContactEvents below to resolve a contact's BodyHandles
    // back to entities. Safe to build once per Play/Step session and reuse for its whole
    // duration -- no entity is created or destroyed mid-Play today (see Script::OnDestroy's
    // own comment).
    void BuildPhysicsWorld(PhysicsWorld &physicsWorld, Scene &scene,
                           std::unordered_map<uint32_t, int> &outBodyToEntity);

    // Drain this tick's contact events (PhysicsWorld::ConsumeContactEvents) and dispatch
    // Script::OnCollisionEnter/Stay/Exit to both entities involved, resolved via
    // `bodyToEntity` (BuildPhysicsWorld's out-param above). Call once per fixed tick, right
    // after PhysicsWorld::Step() -- see core/scene/script.h's OnCollision* comment for the
    // self/other/point/normal convention.
    void DispatchContactEvents(PhysicsWorld &physicsWorld, Scene &scene,
                               const std::unordered_map<uint32_t, int> &bodyToEntity);

} // namespace toon
