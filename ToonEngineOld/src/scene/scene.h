// Scene and entity types.
//
// A Scene holds a flat vector of entities plus a parent-index on each entity
// that forms a tree. Index 0 is always the implicit root ("Scene"); it has
// no transform component and acts as the world-space anchor for its
// descendants. Every other entity has parent >= 0 (defaulting to 0).
//
// World transforms are a cache (Entity::worldMatrix) computed once per frame
// by UpdateSceneTransforms() before rendering. Entities without a transform
// component inherit their parent's world matrix unchanged.

#pragma once

#include "core/animation.h"
#include "core/animator.h"
#include "core/material.h"
#include "core/renderer.h"
#include "core/transform.h"
#include "scene/model_loader.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

enum class ShadingMode {
    VertexColor,  // triangle.vert + triangle.frag (vertex-colored demo geometry)
    Toon,         // model.vert + toon.frag + outline pass
    Sprite,       // sprite.vert + sprite.frag (unlit, alpha-blended, atlas UV rect)
};

// Light entities have no geometry — they contribute to the toon shading pass.
constexpr int kMaxLights = 8;

enum class LightType { Directional, Point };

struct Light {
    LightType type      = LightType::Directional;
    glm::vec3 color{1.0f};
    float     intensity = 1.0f;
    glm::vec3 direction{0.3f, 1.0f, 0.5f};  // directional: toward the light
    float     radius    = 10.0f;             // point: falloff distance
};

struct Entity {
    std::string name;

    // Hierarchy. parent == -1 marks the scene root (index 0 only).
    // All other entities default to being a direct child of the root.
    int parent = 0;

    // Optional transform component. nullopt => entity inherits its parent
    // world matrix as-is (used for the scene root and for logical grouping
    // nodes that shouldn't offset their children).
    std::optional<Transform> transform = Transform{};

    // Cached world-space matrix, refreshed by UpdateSceneTransforms().
    glm::mat4 worldMatrix{1.0f};

    std::vector<SubMesh> subMeshes;
    ShadingMode          shading = ShadingMode::Toon;
    std::string          modelPath;  // source file (for serialization)

    // Skeletal animation (empty skeleton = not skinned).
    Skeleton                   skeleton;
    std::vector<AnimationClip> clips;
    Animator                   animator;
    bool                       skinned = false;

    // Sprite component (active when shading == Sprite).
    glm::vec4 spriteTint{1.0f};
    glm::vec4 spriteUVRect{0.0f, 0.0f, 1.0f, 1.0f};  // xy=offset, zw=scale
    bool      spriteFlipX = false;
    bool      spriteFlipY = false;

    // Light (nullopt = not a light entity).
    std::optional<Light> light;
};

struct Scene {
    std::vector<Entity> entities;
    int                 selected = -1;  // index into entities, -1 = none
};

// Ensure the scene has a root entity at index 0 (creating one if missing).
// The root has no transform component and parent = -1. A pre-existing
// entity at index 0 counts as the root only if it matches that shape.
inline void EnsureSceneRoot(Scene& scene) {
    const bool hasRoot = !scene.entities.empty()
        && scene.entities[0].parent == -1
        && !scene.entities[0].transform.has_value();
    if (hasRoot) return;

    Entity root;
    root.name = "Scene";
    root.parent = -1;
    root.transform.reset();  // root has no transform component
    scene.entities.insert(scene.entities.begin(), std::move(root));
    // Shift all existing parent indices by +1 (they now sit after the root)
    // and re-parent previously-orphaned entities to the root.
    for (size_t i = 1; i < scene.entities.size(); ++i) {
        int& p = scene.entities[i].parent;
        p = (p < 0) ? 0 : p + 1;
    }
    if (scene.selected >= 0) scene.selected += 1;
}

// Recompute Entity::worldMatrix for every entity. Assumes parents appear
// before children in the vector (true for construction-order insertion).
inline void UpdateSceneTransforms(Scene& scene) {
    for (size_t i = 0; i < scene.entities.size(); ++i) {
        Entity& e = scene.entities[i];
        glm::mat4 parentW = (e.parent >= 0 && e.parent < static_cast<int>(i))
            ? scene.entities[e.parent].worldMatrix
            : glm::mat4(1.0f);
        glm::mat4 local = e.transform ? TransformMatrix(*e.transform)
                                      : glm::mat4(1.0f);
        e.worldMatrix = parentW * local;
    }
}

// ---------------------------------------------------------------------------
// Hierarchy mutation (defined in scene.cpp).
// ---------------------------------------------------------------------------

// True if `idx` or any of its ancestors equals `maybeAncestor`.
bool IsAncestorOrSelf(const Scene& scene, int idx, int maybeAncestor);

// Append a new child entity under `parent` (defaults to root on bad index).
// Returns the new entity's index.
int AddChildEntity(Scene& scene, int parent, const char* name);

// Append a new light entity under `parent`. Returns the new entity's index.
int AddLightEntity(Scene& scene, int parent, LightType type);

// Delete an entity and its entire subtree. No-op for the root.
// Releases GPU resources and fixes up parent / selected indices.
void DeleteEntity(Scene& scene, int idx);

// Re-parent `idx` to `newParent`. Preserves the entity's world transform
// by converting into the new parent's local frame. Refuses cycles, the root
// itself, and no-op moves. Reorders so parents still precede children.
bool ReparentEntity(Scene& scene, int idx, int newParent);

// Move `idx` to be a sibling of `target` (same parent), positioned just
// before or just after `target` in its parent's child list. Reparents
// first if needed (preserving world transform). Refuses the root, cycles,
// and no-op moves. Returns true on success.
bool MoveEntityAsSibling(Scene& scene, int idx, int target, bool before);

// Duplicate an entity and its entire subtree as a new sibling of `idx`.
// Mesh data is re-loaded from modelPath when available; non-model entities
// clone everything except GPU resources (subMeshes are left empty).
// Returns the duplicated root's new index, or -1 on failure.
int DuplicateEntity(Scene& scene, int idx);

// Release all GPU resources owned by every entity in the scene.
inline void DestroyScene(Scene& scene) {
    for (auto& entity : scene.entities) {
        for (auto& sm : entity.subMeshes) {
            DestroyMesh(sm.mesh);
            DestroyTexture(sm.material.texture);
            DestroyTexture(sm.material.normalMap);
        }
    }
    scene.entities.clear();
    scene.selected = -1;
}
