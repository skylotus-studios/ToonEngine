//============================================================================
//  app/physics_glue.cpp — see physics_glue.h.
//============================================================================
#include "app/physics_glue.h"

#include <algorithm> // std::max -- ScaledColliderExtents' non-uniform-scale fallback
#include <cmath>     // std::abs -- ScaledColliderExtents' non-uniform-scale detection
#include <cstdio>

namespace toon {

    Vec3 ScaledColliderExtents(const ColliderComponent &collider, const Vec3 &scale, const std::string &entityName,
                               bool logWarnings) {
        switch (collider.shape) {
            case ColliderShape::Box:
                return {collider.extents.x * scale.x, collider.extents.y * scale.y, collider.extents.z * scale.z};
            case ColliderShape::Sphere: {
                const float s = std::max(scale.x, std::max(scale.y, scale.z));
                if (logWarnings && (std::abs(scale.x - scale.y) > 1e-4f || std::abs(scale.y - scale.z) > 1e-4f)) {
                    std::fprintf(stderr,
                                 "Entity '%s': non-uniform scale on a Sphere collider isn't supported -- using the "
                                 "largest axis (%.3f)\n",
                                 entityName.c_str(), s);
                }
                return {collider.extents.x * s, 0.0f, 0.0f};
            }
            case ColliderShape::Capsule: {
                // extents.x = half-height (along the capsule's local Y), extents.y = radius
                // (the X/Z plane) -- see core/physics/physics.h's BodyDesc comment.
                const float radialScale = std::max(scale.x, scale.z);
                if (logWarnings && std::abs(scale.x - scale.z) > 1e-4f) {
                    std::fprintf(stderr,
                                 "Entity '%s': non-uniform (x/z) scale on a Capsule collider isn't supported -- "
                                 "using the larger axis (%.3f)\n",
                                 entityName.c_str(), radialScale);
                }
                return {collider.extents.x * scale.y, collider.extents.y * radialScale, 0.0f};
            }
        }
        return collider.extents;
    }

    void BuildPhysicsWorld(PhysicsWorld &physicsWorld, Scene &scene) {
        physicsWorld.Clear();
        for (Entity &e : scene.entities) {
            if (!e.collider || !e.transform) { continue; }

            // A bare collider (no authored RigidBodyComponent) is an implicit static
            // collider -- a wall/floor. Synthesize one for this Play session only; Stop's
            // `scene = sceneBackup` discards it, same as everything else Play does (see
            // core/scene/scene.h's RigidBodyComponent comment).
            if (!e.body) {
                RigidBodyComponent implicitStatic;
                implicitStatic.type = BodyType::Static;
                e.body = implicitStatic;
            }

            BodyDesc desc;
            desc.shape = e.collider->shape;
            desc.extents = ScaledColliderExtents(*e.collider, e.transform->scale, e.name);
            desc.type = e.body->type;
            desc.mass = e.body->mass;
            desc.friction = e.body->friction;
            desc.restitution = e.body->restitution;
            desc.position = e.transform->position;
            desc.rotation = e.transform->rotation;

            e.body->handle = physicsWorld.CreateBody(desc);
        }
    }

} // namespace toon
