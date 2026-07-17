//============================================================================
//  app/editor_render.cpp — see editor_render.h.
//============================================================================
#include "app/editor_render.h"

#include "app/editor_state.h"
#include "app/physics_glue.h"

#include <vector>

namespace toon {

    void RenderFrame(EditorState &state) {
        Renderer &renderer = state.renderer;
        const Scene &scene = state.scene;

        // Cascaded shadow map pre-pass: walks the same renderable entities as the main pass
        // below, once per cascade, into the shadow map's own depth-only targets. Must run
        // before BeginFrame (separate render targets, no interaction with the main G-buffer).
        // BeginShadowPass returns 0 (the loop below becomes a no-op) when the Settings panel's
        // Shadows toggle is off.
        const uint32_t shadowCascades = renderer.BeginShadowPass();
        for (uint32_t cascade = 0; cascade < shadowCascades; ++cascade) {
            renderer.BeginShadowCascade(cascade);
            for (const Entity &e : scene.entities) {
                if (e.mesh != MeshHandle::Invalid) {
                    renderer.DrawMeshShadow(e.mesh, e.worldMatrix);
                } else if (e.model != ModelHandle::Invalid) {
                    renderer.DrawModelShadow(e.model, e.worldMatrix);
                }
            }
        }
        renderer.EndShadowPass();

        const Color kClearColor{0.10f, 0.11f, 0.13f, 1.0f};
        renderer.BeginFrame(kClearColor);

        // Walk the scene, drawing every renderable entity with its hierarchy-composed world
        // matrix (+ last frame's, for motion vectors). The shared style overlays band count,
        // ambient, and the global outline-width multiplier onto each entity's own material.
        for (const Entity &e : scene.entities) {
            const bool isMesh = e.mesh != MeshHandle::Invalid;
            const bool isModel = e.model != ModelHandle::Invalid;
            if (!isMesh && !isModel) {
                continue; // root / non-renderable
            }

            Material m = e.material;
            m.bands = state.style.bands;
            m.ambient = state.style.ambient;
            m.outlineWidth = e.material.outlineWidth * state.outlineScale;
            if (isMesh) {
                renderer.DrawMesh(e.mesh, e.worldMatrix, e.prevWorldMatrix, m);
            } else {
                renderer.DrawModel(e.model, e.worldMatrix, e.prevWorldMatrix, m);
            }
        }

        // Resolve the HDR scene to the back buffer (post effects + exposure + tone map).
        renderer.EndScene();

        // Collider debug wireframes (M2.1) -- after EndScene, before the UI overlay (see
        // Renderer::DrawWireframe's call-timing contract). A fixed yellow-ish color for every
        // shape; distinguishing static/dynamic by color is future polish, not needed to see
        // whether a collider matches its entity's visual size/position.
        //
        // Mirrors BuildPhysicsWorld exactly (scaled extents via ScaledColliderExtents, a
        // scale-free position/rotation matrix, no parent-chain fold) rather than using
        // e.worldMatrix + raw extents directly -- so the overlay always shows what Jolt is
        // actually simulating, not the renderer's own (possibly-scaled, possibly-nested)
        // placement of the entity. The two agree for today's root-parented, unit-scale demo
        // entities, but only one of them is correct in general.
        if (state.showColliders) {
            const Color wireColor{1.0f, 0.9f, 0.2f, 1.0f};
            for (const Entity &e : scene.entities) {
                if (!e.collider || !e.transform) { continue; }
                const Vec3 scaledExtents =
                    ScaledColliderExtents(*e.collider, e.transform->scale, e.name, /*logWarnings=*/false);
                const Mat4 world = ComposeWorldMatrix(e.transform->position, e.transform->rotation, {1.0f, 1.0f, 1.0f});
                const std::vector<Vec3> wireframe = ColliderWireframe(e.collider->shape, scaledExtents);
                renderer.DrawWireframe(world, wireframe.data(), static_cast<uint32_t>(wireframe.size()), wireColor);
            }
        }
    }

} // namespace toon
