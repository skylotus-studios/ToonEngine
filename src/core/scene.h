#pragma once
//============================================================================
//  core/scene.h — the scene graph: an entity tree with hierarchy-composed world
//  transforms.
//
//  Backend-agnostic like the rest of the seam vocabulary (toon::Transform + Mat4 +
//  opaque handles) — no Diligent types leak here. The world-matrix composition (the
//  actual 4x4 math) lives in scene.cpp, which IS allowed to use Diligent
//  (build-on-Diligent — see CLAUDE.md). Adapted from ToonEngineOld/src/scene.
//============================================================================
#include "core/renderer.h"   // Transform, Material, Mesh/ModelHandle, Mat4 (via math.h)

#include <optional>
#include <string>
#include <vector>

namespace toon {

// One node in the scene. It renders either a procedural primitive (`mesh` set) or a loaded
// glTF model (`model` set); `material` is the primitive's material, or the model's
// style/tint. `transform == nullopt` marks a pure grouping/anchor node (the root) that
// simply passes its parent's world matrix through.
struct Entity {
    std::string name;
    int         parent = 0;                 // parent index; -1 marks the root (index 0 only)

    std::optional<Transform> transform = Transform{};   // local placement
    Mat4 worldMatrix;                       // cached: local * parent.world (UpdateWorldTransforms)
    Mat4 prevWorldMatrix;                   // last frame's world, for motion vectors

    MeshHandle  mesh     = MeshHandle::Invalid;   // a procedural primitive, or...
    ModelHandle model    = ModelHandle::Invalid;  // ...a loaded glTF model
    Material    material;                          // primitive material / model tint + style
};

// A scene is a flat vector with parents ALWAYS before their children, so a single forward
// pass composes world transforms. Index 0 is the implicit root.
struct Scene {
    std::vector<Entity> entities;
};

// Ensure a root exists at index 0 (parent = -1, no transform). Call once, before adding
// entities; existing entities are shifted down and re-parented to the new root.
void EnsureSceneRoot(Scene& scene);

// Append a new entity under `parent` (defaults to the root on a bad index) and return its
// index. Build the scene parent-first so the parents-before-children invariant holds.
int AddEntity(Scene& scene, int parent, const char* name);

// Recompute every entity's cached worldMatrix from the hierarchy (one forward pass, since
// parents precede children), snapshotting the previous frame's matrices first. Call once
// per frame before drawing.
void UpdateWorldTransforms(Scene& scene);

// Drop all entities. GPU meshes/models are owned by the Renderer and freed at its Shutdown,
// so the scene only holds handles — nothing GPU-side to release here.
inline void DestroyScene(Scene& scene) { scene.entities.clear(); }

} // namespace toon
