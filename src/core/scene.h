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
    int                 selected = -1;   // index into entities; -1 = none (editor selection)
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

// Set entity `idx`'s transform from a WORLD matrix (the editor gizmo edits in world space):
// folds out the parent's world and decomposes to the local TRS the renderer recomposes. No-op
// on a transform-less anchor. Call between UpdateWorldTransforms passes (uses cached worlds).
void SetEntityWorldMatrix(Scene& scene, int idx, const Mat4& world);

// --- Hierarchy mutations (editor operations; defined in scene.cpp) ----------
// All keep the parents-before-children invariant (re-ordering as needed) and fix up
// `selected`. They mutate the vector, so never call them mid-iteration over `entities`.

// True if `idx` equals `maybeAncestor` or has it anywhere up its parent chain.
bool IsAncestorOrSelf(const Scene& scene, int idx, int maybeAncestor);

// Append a new child under `parent`, re-ordered to sit under its parent's subtree in the
// outliner. Returns the new entity's index.
int AddChildEntity(Scene& scene, int parent, const char* name);

// Delete an entity and its whole subtree (no-op on the root).
void DeleteEntity(Scene& scene, int idx);

// Duplicate an entity + its subtree as a sibling (copies mesh/model handles + material;
// models stay shared by handle). Returns the duplicate root's new index, or -1 on failure.
int DuplicateEntity(Scene& scene, int idx);

// Re-parent `idx` under `newParent`, preserving its WORLD transform — the local TRS is
// rewritten (via the decompose) so the object doesn't jump. Refuses the root, cycles, and
// no-ops. Returns true on success.
bool ReparentEntity(Scene& scene, int idx, int newParent);

// Move `idx` to be a sibling just before/after `target` (re-parenting first if needed, also
// world-preserving).
bool MoveEntityAsSibling(Scene& scene, int idx, int target, bool before);

// Drop all entities. GPU meshes/models are owned by the Renderer and freed at its Shutdown,
// so the scene only holds handles — nothing GPU-side to release here.
inline void DestroyScene(Scene& scene) { scene.entities.clear(); }

} // namespace toon
