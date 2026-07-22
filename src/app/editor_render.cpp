//============================================================================
//  app/editor_render.cpp: see editor_render.h.
//============================================================================
#include "app/editor_render.h"

#include "app/editor_state.h"
#include "app/physics_glue.h"
#include "app/picking.h"

#include <algorithm>
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
                    // Roadmap #11: an animated entity's shadow follows its animated pose, not
                    // a static bind-pose one -- see Renderer::DrawModelShadow's AnimationState
                    // param. Omitted (nullptr) for an unanimated model, the pre-#11 path
                    // unchanged, so a static model never pays for a second ComputeTransforms.
                    if (e.animation) {
                        const AnimationState anim{e.animation->clipIndex, e.animation->time, e.animation->prevTime};
                        renderer.DrawModelShadow(e.model, e.worldMatrix, &anim);
                    } else {
                        renderer.DrawModelShadow(e.model, e.worldMatrix);
                    }
                }
            }
        }
        renderer.EndShadowPass();

        const Color kClearColor{0.10f, 0.11f, 0.13f, 1.0f};
        renderer.BeginFrame(kClearColor);

        // Editor backdrop (roadmap #12): the gradient sky draws first, into the HDR G-buffer
        // BeginFrame just cleared, so every later opaque entity draws over it. When off, the
        // flat kClearColor above shows through instead.
        if (state.showSky) { renderer.DrawSky(state.skyTop, state.skyBottom); }

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
            } else if (e.animation) {
                // Roadmap #11: see the shadow pre-pass loop above for why this is
                // conditional rather than always constructing an AnimationState.
                const AnimationState anim{e.animation->clipIndex, e.animation->time, e.animation->prevTime};
                renderer.DrawModel(e.model, e.worldMatrix, e.prevWorldMatrix, m, &anim);
            } else {
                renderer.DrawModel(e.model, e.worldMatrix, e.prevWorldMatrix, m);
            }
        }

        // Transparent sprite pass (roadmap #13): after opaque geometry, before EndScene
        // resolves the HDR scene -- DrawSprite needs the still-bound G-buffer + scene depth
        // (see its own call-timing contract). Gathered and sorted back-to-front (farthest
        // first) by VIEW-SPACE depth -- the distance along the camera's forward axis, not
        // raw distance to the camera -- so draw order matches what the depth buffer would
        // decide; a straight-line sort can mis-order two sprites at equal radial distance but
        // different screen positions. Depth WRITES are off (CreateSpritePipeline), so this
        // draw order is what actually composites overlapping sprites correctly, not just a
        // performance nicety.
        {
            Vec3 eye, forward, up;
            CameraWorldBasis(state.camera, eye, forward, up);

            std::vector<const Entity *> spriteEntities;
            for (const Entity &e : scene.entities) {
                if (e.sprite && e.sprite->texture != TextureHandle::Invalid) { spriteEntities.push_back(&e); }
            }
            std::sort(spriteEntities.begin(), spriteEntities.end(), [&](const Entity *a, const Entity *b) {
                const Vec3 posA{a->worldMatrix.m[12], a->worldMatrix.m[13], a->worldMatrix.m[14]};
                const Vec3 posB{b->worldMatrix.m[12], b->worldMatrix.m[13], b->worldMatrix.m[14]};
                return Dot(posA - eye, forward) > Dot(posB - eye, forward); // farthest first
            });

            for (const Entity *e : spriteEntities) {
                // Flip by negating the relevant axis's offset/scale (ToonEngineOld's
                // convention), not a shader branch -- see SpriteComponent's own comment.
                Vec4 uvRect = e->sprite->uvRect;
                if (e->sprite->flipX) {
                    uvRect.x += uvRect.z;
                    uvRect.z = -uvRect.z;
                }
                if (e->sprite->flipY) {
                    uvRect.y += uvRect.w;
                    uvRect.w = -uvRect.w;
                }
                renderer.DrawSprite(e->worldMatrix, e->prevWorldMatrix, e->sprite->texture, e->sprite->tint, uvRect);
            }
        }

        // Resolve the HDR scene to the back buffer (post effects + exposure + tone map).
        renderer.EndScene();

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
        // what's actually on screen.
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
