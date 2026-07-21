//============================================================================
//  core/scene/scene.cpp: scene graph world-transform composition.
//
//  The one engine translation unit besides renderer.cpp that uses Diligent: it composes
//  world matrices with Diligent's float4x4 (build-on-Diligent, no hand-rolled 4x4 math)
//  and hands them across the seam as the plain toon::Mat4. Everything the app sees stays
//  Diligent-free (see scene.h).
//============================================================================
#include "core/scene/scene.h"

#include "BasicMath.hpp" // Diligent float4x4

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace Diligent;

namespace toon {

    namespace {

        // object -> local matrix from a Transform. MUST match renderer.cpp's WorldFromTransform
        // convention (row-major, scale · R · translation) so scene-composed and single-object
        // world matrices agree.
        float4x4 LocalFromTransform(const Transform &t) {
            const QuaternionF q(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
            return float4x4::Scale(t.scale.x, t.scale.y, t.scale.z) * q.ToMatrix() *
                   float4x4::Translation(t.position.x, t.position.y, t.position.z);
        }

        // Component-wise lerp between two sim ticks' local poses, for UpdateWorldTransforms' render
        // interpolation (see scene.h). Rotation is a proper spherical interpolation (slerp) between
        // the two quaternions -- unlike lerping Euler angles, this always takes the short way round
        // and never gimbal-wobbles, even across a fast spin or a large single-tick rotation.
        Transform LerpTransform(const Transform &a, const Transform &b, float t) {
            Transform out;
            out.position = a.position + (b.position - a.position) * t;
            const QuaternionF qa(a.rotation.x, a.rotation.y, a.rotation.z, a.rotation.w);
            const QuaternionF qb(b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w);
            const QuaternionF qs = slerp(qa, qb, t);
            out.rotation = {qs.q.x, qs.q.y, qs.q.z, qs.q.w};
            out.scale = a.scale + (b.scale - a.scale) * t;
            return out;
        }

        // Plain Mat4 (seam) <-> Diligent float4x4: both row-major, a straight element copy.
        Mat4 ToMat4(const float4x4 &m) {
            Mat4 out;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out.m[r * 4 + c] = m[r][c];
                }
            }
            return out;
        }
        float4x4 ToFloat4x4(const Mat4 &in) {
            float4x4 out;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out[r][c] = in.m[r * 4 + c];
                }
            }
            return out;
        }

        // Decompose a local (object->parent) matrix back into a Transform: the exact inverse of
        // LocalFromTransform (Scale · R, row-vector). Used by the gizmo write-back + world-preserving
        // reparent, so a matrix round-trips to the same TRS the renderer would rebuild.
        void DecomposeToTransform(const float4x4 &m, Transform &out) {
            // Translation is the 4th row (row-vector convention: local = S·R·T).
            out.position = {m[3][0], m[3][1], m[3][2]};

            // The upper-left 3x3 rows are s_i · R.row(i) (scale is leftmost, so it scales each row).
            float3 r0{m[0][0], m[0][1], m[0][2]};
            float3 r1{m[1][0], m[1][1], m[1][2]};
            float3 r2{m[2][0], m[2][1], m[2][2]};
            float sx = length(r0), sy = length(r1), sz = length(r2);

            // A negative determinant means a mirrored axis; fold the sign into X so what's left is a
            // proper rotation (the usual decompose convention).
            if (dot(cross(r0, r1), r2) < 0.0f) {
                sx = -sx;
                r0 = -r0;
            }
            out.scale = {sx, sy, sz};

            if (std::abs(sx) > 1e-8f) { r0 = r0 / sx; }
            if (sy > 1e-8f) { r1 = r1 / sy; }
            if (sz > 1e-8f) { r2 = r2 / sz; }

            // [r0;r1;r2] is now a proper (unscaled) rotation matrix -- hand it to Diligent's own
            // matrix->quaternion conversion rather than re-deriving Euler angles by hand (the old
            // atan2/asin extraction this replaced had its own gimbal-lock branch to worry about;
            // FromRotationMatrix needs none).
            float4x4 rot = float4x4::Identity();
            rot[0][0] = r0.x;
            rot[0][1] = r0.y;
            rot[0][2] = r0.z;
            rot[1][0] = r1.x;
            rot[1][1] = r1.y;
            rot[1][2] = r1.z;
            rot[2][0] = r2.x;
            rot[2][1] = r2.y;
            rot[2][2] = r2.z;
            const QuaternionF q = QuaternionF::FromRotationMatrix(rot);
            out.rotation = {q.q.x, q.q.y, q.q.z, q.q.w};
        }

        // --- Hierarchy re-ordering helpers (plain index/vector work) ---------------
        // The mutations keep parents-before-children by rebuilding a child list and re-emitting
        // the entities in pre-order DFS (adapted from ToonEngineOld/src/scene/scene.cpp).

        std::vector<std::vector<int>> BuildChildrenList(const Scene &scene, int &outRoot) {
            const int n = static_cast<int>(scene.entities.size());
            std::vector<std::vector<int>> children(n);
            outRoot = -1;
            for (int i = 0; i < n; ++i) {
                const int p = scene.entities[i].parent;
                if (p < 0) {
                    outRoot = i;
                } else if (p < n) {
                    children[p].push_back(i);
                }
            }
            return children;
        }

        // Pre-order DFS from `root` -> newForOld[oldIndex] = new (post-reorder) index.
        std::vector<int> TopoOrderFromChildren(int n, int root, const std::vector<std::vector<int>> &children) {
            std::vector<int> newForOld(n, -1);
            int next = 0;
            auto visit = [&](auto &self, int i) -> void {
                newForOld[i] = next++;
                for (int c : children[i]) {
                    self(self, c);
                }
            };
            if (root >= 0) { visit(visit, root); }
            for (int i = 0; i < n; ++i) { // append any stray orphans (shouldn't happen)
                if (newForOld[i] == -1) { newForOld[i] = next++; }
            }
            return newForOld;
        }

        std::vector<int> BuildTopoOrder(const Scene &scene) {
            int root = -1;
            const auto children = BuildChildrenList(scene, root);
            return TopoOrderFromChildren(static_cast<int>(scene.entities.size()), root, children);
        }

        // Reorder entities by newForOld, patching parent indices + `selected` to the new indexing.
        void ApplyReorder(Scene &scene, const std::vector<int> &newForOld) {
            const int n = static_cast<int>(scene.entities.size());
            std::vector<Entity> reordered(n);
            for (int i = 0; i < n; ++i) {
                reordered[newForOld[i]] = std::move(scene.entities[i]);
            }
            for (Entity &e : reordered) {
                if (e.parent >= 0 && e.parent < n) { e.parent = newForOld[e.parent]; }
            }
            if (scene.selected >= 0 && scene.selected < n) { scene.selected = newForOld[scene.selected]; }
            scene.entities = std::move(reordered);
        }

        // Rewrite entity `idx`'s local transform so its WORLD transform is unchanged after moving
        // under `newParent`: newLocal = currentWorld · newParentWorld⁻¹. Uses the cached world
        // matrices (valid between UpdateWorldTransforms calls). No-op for a transform-less anchor.
        void PreserveWorldOnReparent(Scene &scene, int idx, int newParent) {
            Entity &e = scene.entities[idx];
            if (!e.transform) { return; }
            const float4x4 childWorld = ToFloat4x4(e.worldMatrix);
            const float4x4 newParentWorld = ToFloat4x4(scene.entities[newParent].worldMatrix);
            DecomposeToTransform(childWorld * newParentWorld.Inverse(), *e.transform);
        }

    } // namespace

    // --- Entity copy semantics (M1.3) --------------------------------------------
    // A std::unique_ptr inside ScriptComponent makes Entity's implicit copy operations
    // deleted (see scene.h); these deep-clone `scripts` via the name registry and each
    // script's own Save/Load instead. Everything else is a plain member copy: mesh/model
    // handles are just IDs (never touches the Renderer, so no GPU re-upload/leak).

    Entity::Entity(const Entity &other)
        : name(other.name), parent(other.parent), transform(other.transform), prevSimTransform(other.prevSimTransform),
          worldMatrix(other.worldMatrix), prevWorldMatrix(other.prevWorldMatrix), mesh(other.mesh), model(other.model),
          material(other.material), primitive(other.primitive), modelPath(other.modelPath), light(other.light),
          collider(other.collider), body(other.body), audioSource(other.audioSource), animation(other.animation) {
        scripts.reserve(other.scripts.size());
        for (const ScriptComponent &src : other.scripts) {
            ScriptComponent dup;
            dup.name = src.name;
            dup.instance = CreateScript(src.name);
            if (dup.instance && src.instance) {
                // Round-trip through a memory buffer, not the entity's own fields: a
                // script's Save/Load only ever touches its own state (see script.h).
                std::ostringstream buf;
                src.instance->Save(buf);
                std::istringstream in(buf.str());
                dup.instance->Load(in);
            }
            scripts.push_back(std::move(dup));
        }
    }

    Entity &Entity::operator=(const Entity &other) {
        if (this == &other) { return *this; }
        Entity tmp(other);      // copy-construct via the ctor above (exception-safe: `this`
        *this = std::move(tmp); // is untouched if CreateScript/Save/Load throws), then move in
        return *this;
    }

    void EnsureSceneRoot(Scene &scene) {
        const bool hasRoot =
            !scene.entities.empty() && scene.entities[0].parent == -1 && !scene.entities[0].transform.has_value();
        if (hasRoot) { return; }

        Entity root;
        root.name = "Scene";
        root.parent = -1;
        root.transform.reset(); // the root is a pure world anchor (no local transform)
        scene.entities.insert(scene.entities.begin(), std::move(root));

        // Existing entities now sit one slot later: shift their parent indices by +1, and
        // re-parent any previously-rootless entity to the new root at 0.
        for (size_t i = 1; i < scene.entities.size(); ++i) {
            int &p = scene.entities[i].parent;
            p = (p < 0) ? 0 : p + 1;
        }
    }

    int AddEntity(Scene &scene, int parent, const char *name) {
        const int n = static_cast<int>(scene.entities.size());
        if (parent < 0 || parent >= n) {
            parent = 0; // default: under the root
        }

        Entity e;
        e.name = name ? name : "Entity";
        e.parent = parent;
        scene.entities.push_back(std::move(e));
        return static_cast<int>(scene.entities.size()) - 1;
    }

    void SnapshotSimState(Scene &scene) {
        for (Entity &e : scene.entities) {
            if (e.transform) { e.prevSimTransform = e.transform; }
            // Roadmap #11 (skeletal animation): same "snapshot before this tick advances it"
            // treatment as transform above, so Renderer::DrawModel's AnimationState carries a
            // real previous pose for motion vectors, not last tick's time reused twice.
            if (e.animation) { e.animation->prevTime = e.animation->time; }
        }
    }

    void UpdateWorldTransforms(Scene &scene, float alpha) {
        const int n = static_cast<int>(scene.entities.size());
        for (int i = 0; i < n; ++i) {
            Entity &e = scene.entities[i];
            e.prevWorldMatrix = e.worldMatrix; // snapshot last frame first (motion vectors)

            // Parent's already-computed world (parents precede children); identity for the root
            // or any entity without a valid earlier parent.
            const float4x4 parentW = (e.parent >= 0 && e.parent < i) ? ToFloat4x4(scene.entities[e.parent].worldMatrix)
                                                                     : float4x4::Identity();
            // Render the pose interpolated between the previous and current sim tick (fixed-
            // timestep decoupling -- see scene.h). No prior sim tick yet (fresh/loaded entity)
            // -> interpolate transform with itself, i.e. render it exactly. Row-vector convention
            // (v' = v·M): apply local first, then the parent.
            float4x4 local = float4x4::Identity();
            if (e.transform) {
                const Transform interpolated =
                    LerpTransform(e.prevSimTransform.value_or(*e.transform), *e.transform, alpha);
                local = LocalFromTransform(interpolated);
            }
            e.worldMatrix = ToMat4(local * parentW);
        }
    }

    void SetEntityWorldMatrix(Scene &scene, int idx, const Mat4 &world) {
        const int n = static_cast<int>(scene.entities.size());
        if (idx < 0 || idx >= n) { return; }
        Entity &e = scene.entities[idx];
        if (!e.transform) {
            return; // a pure anchor has no local placement to write
        }

        // world = local · parent  =>  local = world · parent⁻¹, then decompose to TRS. The gizmo
        // edits the entity's world matrix; this folds the parent back out so the stored local
        // Transform (which the renderer recomposes) matches what the gizmo showed.
        const float4x4 parentW =
            (e.parent >= 0 && e.parent < n) ? ToFloat4x4(scene.entities[e.parent].worldMatrix) : float4x4::Identity();
        DecomposeToTransform(ToFloat4x4(world) * parentW.Inverse(), *e.transform);
    }

    Transform MakeLightTransform(const Vec3 &position, const Vec3 &dirToLight) {
        float3 fwd(dirToLight.x, dirToLight.y, dirToLight.z);
        const float len = length(fwd);
        fwd = len > 1e-8f ? fwd / len : float3(0.0f, 0.0f, 1.0f);

        // Any up-reference not parallel to fwd works; fall back to +X when fwd is near-vertical.
        const float3 upRef = (std::abs(fwd.y) > 0.999f) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
        float3 right = cross(upRef, fwd);
        const float rlen = length(right);
        right = rlen > 1e-8f ? right / rlen : float3(1.0f, 0.0f, 0.0f);
        const float3 up = cross(fwd, right); // unit length: fwd, right already orthonormal

        // Rows = world axes [right, up, forward] (row-vector convention, matches
        // LocalFromTransform); local +Z maps to world `fwd`, i.e. `dirToLight`.
        float4x4 rot = float4x4::Identity();
        rot[0][0] = right.x;
        rot[0][1] = right.y;
        rot[0][2] = right.z;
        rot[1][0] = up.x;
        rot[1][1] = up.y;
        rot[1][2] = up.z;
        rot[2][0] = fwd.x;
        rot[2][1] = fwd.y;
        rot[2][2] = fwd.z;

        Transform out;
        DecomposeToTransform(rot, out); // extracts rotation (position/scale overwritten below)
        out.position = position;
        out.scale = {1.0f, 1.0f, 1.0f};
        return out;
    }

    Mat4 ComposeWorldMatrix(const Vec3 &position, const Quat &rotation, const Vec3 &scale) {
        return ToMat4(LocalFromTransform(Transform{position, rotation, scale}));
    }

    bool GetActiveLight(const Scene &scene, Vec3 &dirToLight, Vec3 &color, float &intensity) {
        for (const Entity &e : scene.entities) {
            if (!e.light) { continue; }
            // Direction-to-light = world image of local +Z = row 2 of the world matrix (see
            // MakeLightTransform for the matching convention). Mat4 is row-major (math.h).
            const Vec3 fwd{e.worldMatrix.m[8], e.worldMatrix.m[9], e.worldMatrix.m[10]};
            if (Length(fwd) < 1e-8f) {
                continue; // degenerate (e.g. zero-scaled) -- keep looking
            }
            dirToLight = Normalize(fwd);
            color = e.light->color;
            intensity = e.light->intensity;
            return true;
        }
        return false;
    }

    // --- Hierarchy mutations ----------------------------------------------------

    bool IsAncestorOrSelf(const Scene &scene, int idx, int maybeAncestor) {
        const int n = static_cast<int>(scene.entities.size());
        int guard = 0;
        while (idx >= 0 && idx < n && guard++ < n) {
            if (idx == maybeAncestor) { return true; }
            idx = scene.entities[idx].parent;
        }
        return false;
    }

    int AddChildEntity(Scene &scene, int parent, const char *name) {
        const int n = static_cast<int>(scene.entities.size());
        if (parent < 0 || parent >= n) {
            parent = 0; // default: under the root
        }

        Entity e;
        e.name = name ? name : "Entity";
        e.parent = parent;
        scene.entities.push_back(std::move(e));

        // Re-order so the new entity lands right after its parent's subtree in the outliner.
        const int addedOld = static_cast<int>(scene.entities.size()) - 1;
        const std::vector<int> newForOld = BuildTopoOrder(scene);
        const int addedNew = newForOld[addedOld];
        ApplyReorder(scene, newForOld);
        return addedNew;
    }

    void DeleteEntity(Scene &scene, int idx) {
        const int n = static_cast<int>(scene.entities.size());
        if (idx <= 0 || idx >= n) {
            return; // never delete the root
        }
        if (scene.entities[idx].parent == -1) { return; }

        // Mark idx + everything whose ancestor chain includes it (iterate to a fixpoint).
        std::vector<char> kill(n, 0);
        kill[idx] = 1;
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < n; ++i) {
                if (kill[i]) { continue; }
                const int p = scene.entities[i].parent;
                if (p >= 0 && p < n && kill[p]) {
                    kill[i] = 1;
                    changed = true;
                }
            }
        }

        // Compact survivors; build the old->new index map and patch parents + selection.
        std::vector<int> newForOld(n, -1);
        std::vector<Entity> kept;
        int next = 0;
        for (int i = 0; i < n; ++i) {
            if (!kill[i]) {
                newForOld[i] = next++;
                kept.push_back(std::move(scene.entities[i]));
            }
        }
        for (Entity &e : kept) {
            if (e.parent >= 0 && e.parent < n) { e.parent = newForOld[e.parent]; }
        }
        scene.selected = (scene.selected >= 0 && scene.selected < n) ? newForOld[scene.selected] : -1;
        scene.entities = std::move(kept);
    }

    int DuplicateEntity(Scene &scene, int idx) {
        const int n = static_cast<int>(scene.entities.size());
        if (idx <= 0 || idx >= n) { return -1; }
        if (scene.entities[idx].parent == -1) { return -1; }

        // DFS the subtree so each child's parent is cloned before it.
        int root = -1;
        const auto children = BuildChildrenList(scene, root);
        std::vector<int> subtree;
        auto collect = [&](auto &self, int i) -> void {
            subtree.push_back(i);
            for (int c : children[i]) {
                self(self, c);
            }
        };
        collect(collect, idx);

        const int base = n;
        std::vector<int> oldToNew(n, -1);
        for (size_t k = 0; k < subtree.size(); ++k) {
            oldToNew[subtree[k]] = base + static_cast<int>(k);
        }

        for (int oldIdx : subtree) {
            Entity dup = scene.entities[oldIdx]; // copies transform/mesh/model/material (handles
                                                 // shared); scripts deep-clone via the registry
                                                 // (Entity's copy ctor, above)
            if (oldIdx == idx) {
                dup.name += " (copy)";
            } else {
                dup.parent = oldToNew[scene.entities[oldIdx].parent];
            }
            scene.entities.push_back(std::move(dup));
        }

        // Place the duplicate root right after the original, then re-topo-order.
        int rootAfter = -1;
        auto childrenAfter = BuildChildrenList(scene, rootAfter);
        auto &sibs = childrenAfter[scene.entities[base].parent];
        sibs.erase(std::remove(sibs.begin(), sibs.end(), base), sibs.end());
        auto it = std::find(sibs.begin(), sibs.end(), idx);
        if (it != sibs.end()) { ++it; }
        sibs.insert(it, base);

        const std::vector<int> newForOld =
            TopoOrderFromChildren(static_cast<int>(scene.entities.size()), rootAfter, childrenAfter);
        const int result = newForOld[base];
        ApplyReorder(scene, newForOld);
        return result;
    }

    bool ReparentEntity(Scene &scene, int idx, int newParent) {
        const int n = static_cast<int>(scene.entities.size());
        if (idx <= 0 || idx >= n) {
            return false; // root can't be reparented
        }
        if (newParent < 0 || newParent >= n) { return false; }
        if (newParent == idx) { return false; }
        if (IsAncestorOrSelf(scene, newParent, idx)) {
            return false; // would create a cycle
        }
        if (scene.entities[idx].parent == newParent) {
            return false; // no-op
        }

        PreserveWorldOnReparent(scene, idx, newParent); // rewrite local so the object stays put
        scene.entities[idx].parent = newParent;
        ApplyReorder(scene, BuildTopoOrder(scene));
        return true;
    }

    bool MoveEntityAsSibling(Scene &scene, int idx, int target, bool before) {
        const int n = static_cast<int>(scene.entities.size());
        if (idx <= 0 || idx >= n) { return false; }
        if (target <= 0 || target >= n) { return false; }
        if (idx == target) { return false; }
        if (IsAncestorOrSelf(scene, target, idx)) { return false; }

        const int newParent = scene.entities[target].parent;
        if (newParent < 0) {
            return false; // target is the root
        }

        PreserveWorldOnReparent(scene, idx, newParent); // rewrite local so the object stays put
        scene.entities[idx].parent = newParent;

        // Tweak newParent's child order so idx sits just before/after target.
        int root = -1;
        auto children = BuildChildrenList(scene, root);
        auto &sibs = children[newParent];
        sibs.erase(std::remove(sibs.begin(), sibs.end(), idx), sibs.end());
        auto tit = std::find(sibs.begin(), sibs.end(), target);
        if (!before && tit != sibs.end()) { ++tit; }
        sibs.insert(tit, idx);

        ApplyReorder(scene, TopoOrderFromChildren(n, root, children));
        return true;
    }

} // namespace toon
