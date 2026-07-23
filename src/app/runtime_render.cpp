//============================================================================
//  app/runtime_render.cpp: see runtime_render.h.
//============================================================================
#include "app/runtime_render.h"

#include "core/camera/camera.h" // CameraWorldBasis (sprite view-space sort)

#include <algorithm>
#include <vector>

namespace toon {

    void RenderScene(RuntimeState &rs) {
        Renderer &renderer = rs.renderer;
        const Scene &scene = rs.scene;

        // Cascaded shadow map pre-pass: walks the same renderable entities as the main pass
        // below, once per cascade, into the shadow map's own depth-only targets. Must run
        // before BeginFrame (separate render targets, no interaction with the main G-buffer).
        // BeginShadowPass returns 0 (the loop below becomes a no-op) when shadows are off.
        const uint32_t shadowCascades = renderer.BeginShadowPass();
        for (uint32_t cascade = 0; cascade < shadowCascades; ++cascade) {
            renderer.BeginShadowCascade(cascade);
            for (const Entity &e : scene.entities) {
                if (e.mesh != MeshHandle::Invalid) {
                    renderer.DrawMeshShadow(e.mesh, e.worldMatrix);
                } else if (e.model != ModelHandle::Invalid) {
                    // Roadmap #11: an animated entity's shadow follows its animated pose, not
                    // a static bind-pose one. Omitted (nullptr) for an unanimated model, so a
                    // static model never pays for a second ComputeTransforms.
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

        // Sky backdrop (roadmap #12): the gradient sky draws first, into the HDR G-buffer
        // BeginFrame just cleared, so every later opaque entity draws over it. When off, the
        // flat kClearColor above shows through instead. Shared world content, not an editor
        // overlay (unlike the grid), so it's on RuntimeState and drawn here.
        if (rs.showSky) { renderer.DrawSky(rs.skyTop, rs.skyBottom); }

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
            m.bands = rs.style.bands;
            m.ambient = rs.style.ambient;
            m.outlineWidth = e.material.outlineWidth * rs.outlineScale;
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

        // Transparent sprite pass (roadmap #13): after opaque geometry, before EndScene resolves
        // the HDR scene -- DrawSprite needs the still-bound G-buffer + scene depth. Gathered and
        // sorted back-to-front by VIEW-SPACE depth (distance along the camera's forward axis, not
        // raw distance to the camera), so draw order matches what the depth buffer would decide.
        // Depth WRITES are off, so this order is what actually composites overlapping sprites.
        {
            Vec3 eye, forward, up;
            CameraWorldBasis(rs.camera, eye, forward, up);

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
                // Flip by negating the relevant axis's offset/scale (ToonEngineOld's convention),
                // not a shader branch -- see SpriteComponent's own comment.
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
    }

} // namespace toon
