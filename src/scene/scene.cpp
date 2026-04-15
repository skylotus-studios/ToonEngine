// Scene hierarchy operations: add, delete, re-parent, topological re-order.
//
// Mutations preserve the invariant that every entity's parent appears earlier
// in the vector, which UpdateSceneTransforms() relies on.

#include "scene.h"

#include "core/animator.h"
#include "scene/model_loader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <vector>

namespace {

// Decompose a world-or-local matrix back into a Transform (TRS). Good enough
// for re-parenting affine matrices; ignores residual skew/perspective.
Transform DecomposeToTransform(const glm::mat4& m) {
    glm::vec3 scale;
    glm::quat rotation;
    glm::vec3 translation;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, scale, rotation, translation, skew, perspective);

    Transform t;
    t.position = translation;
    t.rotation = glm::degrees(glm::eulerAngles(rotation));
    t.scale    = scale;
    return t;
}

// Build children lists from entity parent fields (order matches vector order).
std::vector<std::vector<int>> BuildChildrenList(const Scene& scene, int& outRoot) {
    const int n = static_cast<int>(scene.entities.size());
    std::vector<std::vector<int>> children(n);
    outRoot = -1;
    for (int i = 0; i < n; ++i) {
        int p = scene.entities[i].parent;
        if (p < 0) outRoot = i;
        else if (p < n) children[p].push_back(i);
    }
    return children;
}

// Pre-order DFS using a (possibly externally-adjusted) children list.
std::vector<int> TopoOrderFromChildren(
        int n, int root, const std::vector<std::vector<int>>& children) {
    std::vector<int> newForOld(n, -1);
    int next = 0;

    auto visit = [&](auto& self, int i) -> void {
        newForOld[i] = next++;
        for (int c : children[i]) self(self, c);
    };
    if (root >= 0) visit(visit, root);

    // Append any orphans (shouldn't happen after EnsureSceneRoot, but be safe).
    for (int i = 0; i < n; ++i) {
        if (newForOld[i] == -1) newForOld[i] = next++;
    }
    return newForOld;
}

std::vector<int> BuildTopoOrder(const Scene& scene) {
    int root = -1;
    auto children = BuildChildrenList(scene, root);
    return TopoOrderFromChildren(static_cast<int>(scene.entities.size()),
                                 root, children);
}

void ApplyReorder(Scene& scene, const std::vector<int>& newForOld) {
    const int n = static_cast<int>(scene.entities.size());
    std::vector<Entity> reordered;
    reordered.resize(n);
    for (int i = 0; i < n; ++i)
        reordered[newForOld[i]] = std::move(scene.entities[i]);

    // Patch parent indices and selected to the new indexing.
    for (auto& e : reordered) {
        if (e.parent >= 0 && e.parent < n) e.parent = newForOld[e.parent];
    }
    if (scene.selected >= 0 && scene.selected < n)
        scene.selected = newForOld[scene.selected];

    scene.entities = std::move(reordered);
}

} // namespace

bool IsAncestorOrSelf(const Scene& scene, int idx, int maybeAncestor) {
    const int n = static_cast<int>(scene.entities.size());
    int guard = 0;
    while (idx >= 0 && idx < n && guard++ < n) {
        if (idx == maybeAncestor) return true;
        idx = scene.entities[idx].parent;
    }
    return false;
}

int AddChildEntity(Scene& scene, int parent, const char* name) {
    const int n = static_cast<int>(scene.entities.size());
    if (parent < 0 || parent >= n) parent = 0;  // default: under root

    Entity e;
    e.name = name ? name : "Entity";
    e.parent = parent;
    e.transform.emplace();
    scene.entities.push_back(std::move(e));

    // DFS-reorder so the new entity lands directly after its parent's
    // existing subtree in the outliner instead of at the bottom.
    const int addedOld = static_cast<int>(scene.entities.size()) - 1;
    std::vector<int> newForOld = BuildTopoOrder(scene);
    int addedNew = newForOld[addedOld];
    ApplyReorder(scene, newForOld);
    return addedNew;
}

int AddLightEntity(Scene& scene, int parent, LightType type) {
    int i = AddChildEntity(scene, parent,
        type == LightType::Directional ? "Directional Light" : "Point Light");
    Entity& e = scene.entities[i];
    e.light = Light{};
    e.light->type = type;
    return i;
}

void DeleteEntity(Scene& scene, int idx) {
    const int n = static_cast<int>(scene.entities.size());
    if (idx <= 0 || idx >= n) return;              // never delete the root
    if (scene.entities[idx].parent == -1) return;  // safety

    // Mark subtree: any entity whose ancestor chain includes idx.
    std::vector<char> kill(n, 0);
    kill[idx] = 1;
    // Propagate: descendants come after their parents only AFTER topo sort.
    // Here we just iterate to fixpoint (cheap for small scenes).
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < n; ++i) {
            if (kill[i]) continue;
            int p = scene.entities[i].parent;
            if (p >= 0 && p < n && kill[p]) { kill[i] = 1; changed = true; }
        }
    }

    // Build old->new index map for survivors; release GPU resources for others.
    std::vector<int> newForOld(n, -1);
    int next = 0;
    for (int i = 0; i < n; ++i) {
        if (kill[i]) {
            Entity& e = scene.entities[i];
            for (auto& sm : e.subMeshes) {
                DestroyMesh(sm.mesh);
                DestroyTexture(sm.material.texture);
                DestroyTexture(sm.material.normalMap);
            }
        } else {
            newForOld[i] = next++;
        }
    }

    std::vector<Entity> kept;
    kept.reserve(static_cast<size_t>(next));
    for (int i = 0; i < n; ++i) {
        if (!kill[i]) kept.push_back(std::move(scene.entities[i]));
    }
    for (auto& e : kept) {
        if (e.parent >= 0 && e.parent < n) e.parent = newForOld[e.parent];
    }

    if (scene.selected >= 0 && scene.selected < n)
        scene.selected = newForOld[scene.selected];  // -1 if killed

    scene.entities = std::move(kept);
}

bool ReparentEntity(Scene& scene, int idx, int newParent) {
    const int n = static_cast<int>(scene.entities.size());
    if (idx <= 0 || idx >= n) return false;         // root can't be reparented
    if (newParent < 0 || newParent >= n) return false;
    if (newParent == idx) return false;
    if (IsAncestorOrSelf(scene, newParent, idx)) return false;  // cycle
    if (scene.entities[idx].parent == newParent) return false;  // no-op

    // Use the most recent cached world matrices to preserve world transform.
    UpdateSceneTransforms(scene);
    glm::mat4 worldIdx   = scene.entities[idx].worldMatrix;
    glm::mat4 parentW    = scene.entities[newParent].worldMatrix;
    glm::mat4 newLocal   = glm::inverse(parentW) * worldIdx;

    scene.entities[idx].parent = newParent;
    if (scene.entities[idx].transform)
        *scene.entities[idx].transform = DecomposeToTransform(newLocal);

    // Keep parents-before-children so UpdateSceneTransforms works next frame.
    ApplyReorder(scene, BuildTopoOrder(scene));
    return true;
}

bool MoveEntityAsSibling(Scene& scene, int idx, int target, bool before) {
    const int n = static_cast<int>(scene.entities.size());
    if (idx <= 0 || idx >= n) return false;
    if (target <= 0 || target >= n) return false;
    if (idx == target) return false;
    if (IsAncestorOrSelf(scene, target, idx)) return false;  // can't drop into own subtree

    const int newParent = scene.entities[target].parent;
    if (newParent < 0) return false;  // target is the root

    // Reparent (preserving world transform) if the parent actually changes.
    if (scene.entities[idx].parent != newParent) {
        UpdateSceneTransforms(scene);
        glm::mat4 worldIdx = scene.entities[idx].worldMatrix;
        glm::mat4 parentW  = scene.entities[newParent].worldMatrix;
        glm::mat4 newLocal = glm::inverse(parentW) * worldIdx;
        scene.entities[idx].parent = newParent;
        if (scene.entities[idx].transform)
            *scene.entities[idx].transform = DecomposeToTransform(newLocal);
    }

    // Build children list, then tweak `newParent`'s child order so `idx`
    // sits just before or after `target`.
    int root = -1;
    auto children = BuildChildrenList(scene, root);
    auto& sibs = children[newParent];
    sibs.erase(std::remove(sibs.begin(), sibs.end(), idx), sibs.end());
    auto tit = std::find(sibs.begin(), sibs.end(), target);
    if (tit == sibs.end()) tit = sibs.end();
    if (!before && tit != sibs.end()) ++tit;
    sibs.insert(tit, idx);

    ApplyReorder(scene, TopoOrderFromChildren(n, root, children));
    return true;
}

int DuplicateEntity(Scene& scene, int idx) {
    const int n = static_cast<int>(scene.entities.size());
    if (idx <= 0 || idx >= n) return -1;
    if (scene.entities[idx].parent == -1) return -1;  // never duplicate root

    // DFS-order the subtree so each child's parent is cloned before it.
    int root = -1;
    auto children = BuildChildrenList(scene, root);
    std::vector<int> subtree;
    auto collect = [&](auto& self, int i) -> void {
        subtree.push_back(i);
        for (int c : children[i]) self(self, c);
    };
    collect(collect, idx);

    // Map original index → new index (new entities are appended in the same order).
    const int base = n;
    std::vector<int> oldToNew(n, -1);
    for (size_t k = 0; k < subtree.size(); ++k)
        oldToNew[subtree[k]] = base + static_cast<int>(k);

    for (int oldIdx : subtree) {
        const Entity& src = scene.entities[oldIdx];
        Entity dup;
        dup.name      = src.name + (oldIdx == idx ? std::string(" (copy)") : std::string());
        dup.transform = src.transform;
        dup.shading   = src.shading;
        dup.modelPath = src.modelPath;
        dup.light     = src.light;
        dup.parent    = (oldIdx == idx) ? src.parent : oldToNew[src.parent];

        // Re-load GPU resources from disk for model-backed entities. Non-
        // model entities (demo primitives) clone metadata with empty meshes.
        if (!src.modelPath.empty()) {
            LoadedModel loaded = LoadModel(src.modelPath.c_str());
            if (!loaded.subMeshes.empty()) {
                dup.subMeshes = std::move(loaded.subMeshes);
                dup.skeleton  = std::move(loaded.skeleton);
                dup.clips     = std::move(loaded.clips);
                dup.skinned   = loaded.skinned;
                if (dup.skinned && !dup.clips.empty())
                    AnimatorSetClip(dup.animator, dup.skeleton, 0);
            }
        }

        scene.entities.push_back(std::move(dup));
    }

    // Position the duplicated subtree as the sibling directly after the
    // original so it's visually adjacent in the outliner.
    int rootAfter = -1;
    auto childrenAfter = BuildChildrenList(scene, rootAfter);
    const int newParent = scene.entities[base].parent;
    auto& sibs = childrenAfter[newParent];
    sibs.erase(std::remove(sibs.begin(), sibs.end(), base), sibs.end());
    auto it = std::find(sibs.begin(), sibs.end(), idx);
    if (it != sibs.end()) ++it;
    sibs.insert(it, base);

    std::vector<int> newForOld = TopoOrderFromChildren(
        static_cast<int>(scene.entities.size()), rootAfter, childrenAfter);
    int resultIdx = newForOld[base];
    ApplyReorder(scene, newForOld);
    return resultIdx;
}
