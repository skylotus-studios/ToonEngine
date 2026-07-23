//============================================================================
//  app/editor_render.cpp: see editor_render.h.
//
//  Since roadmap #15 the scene itself is drawn by app/runtime_render.cpp's RenderScene (shared
//  with the player). RenderFrame calls that, then adds the editor-only authoring trailers --
//  the ground grid, collider debug wireframes, and mouse-pick marker boxes -- which a shipped
//  game never draws (that's why they live here, not in RenderScene).
//============================================================================
#include "app/editor_render.h"

#include "app/editor_state.h"
#include "app/physics_glue.h"
#include "app/picking.h"
#include "app/runtime_render.h"

#include <vector>

namespace toon {

    void RenderFrame(EditorState &state) {
        // The scene, exactly as the player would draw it.
        RenderScene(state.runtime);

        Renderer &renderer = state.runtime.renderer;
        const Scene &scene = state.runtime.scene;

        // Ground grid (roadmap #12) -- after EndScene, before the UI overlay (see
        // Renderer::DrawGrid's call-timing contract: it occludes itself by reading the
        // now-finished scene depth buffer, so it can't run any earlier). An authoring aid,
        // not world content: stays out of Playing AND Paused (still a Play session, just
        // frozen), the same "editor-only" scope as the collider wireframes below.
        if (state.showGrid && state.mode == EditorMode::Editing) { renderer.DrawGrid(); }

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

        // Mouse-pick markers (roadmap #8): a light or empty anchor has no mesh/model/sprite
        // bounds, so it'd otherwise be a dead zone for click-to-select (see app/picking.cpp's
        // kPickBoxHalfExtent fallback box). Reuses ColliderWireframe's Box case rather than a new
        // cube generator -- same shape, sized to exactly match what PickEntity actually tests.
        // A sprite entity (roadmap #13) is excluded: it's already visible (DrawSprite) and
        // picked via its own quad-shaped bounds (picking.cpp's EntityWorldBounds), so a
        // generic marker box floating around it would just be visual noise disconnected from
        // what's actually on screen. Editor-only (an authoring aid), which is why this is here
        // and not in RenderScene -- a shipped game must never draw these boxes.
        {
            const Color markerColor{0.3f, 0.7f, 1.0f, 1.0f};
            const Vec3 markerExtents{kPickBoxHalfExtent, kPickBoxHalfExtent, kPickBoxHalfExtent};
            const std::vector<Vec3> markerWireframe = ColliderWireframe(ColliderShape::Box, markerExtents);
            for (const Entity &e : scene.entities) {
                const bool hasOwnBounds = e.mesh != MeshHandle::Invalid || e.model != ModelHandle::Invalid || e.sprite;
                if (hasOwnBounds || !e.transform) { continue; }
                const Mat4 world = ComposeWorldMatrix(e.transform->position, e.transform->rotation, {1.0f, 1.0f, 1.0f});
                renderer.DrawWireframe(world, markerWireframe.data(), static_cast<uint32_t>(markerWireframe.size()),
                                       markerColor);
            }
        }
    }

} // namespace toon
