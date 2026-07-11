//============================================================================
//  core/scene.cpp — scene graph world-transform composition.
//
//  The one engine translation unit besides renderer.cpp that uses Diligent: it composes
//  world matrices with Diligent's float4x4 (build-on-Diligent — no hand-rolled 4x4 math)
//  and hands them across the seam as the plain toon::Mat4. Everything the app sees stays
//  Diligent-free (see scene.h).
//============================================================================
#include "core/scene.h"

#include "BasicMath.hpp"   // Diligent float4x4

using namespace Diligent;

namespace toon {

namespace {

// object -> local matrix from a Transform. MUST match renderer.cpp's WorldFromTransform
// convention (row-major, scale · Rx · Ry · Rz · translation) so scene-composed and
// single-object world matrices agree.
float4x4 LocalFromTransform(const Transform& t) {
    return float4x4::Scale(t.scale.x, t.scale.y, t.scale.z) *
           float4x4::RotationX(t.rotationEuler.x) *
           float4x4::RotationY(t.rotationEuler.y) *
           float4x4::RotationZ(t.rotationEuler.z) *
           float4x4::Translation(t.position.x, t.position.y, t.position.z);
}

// Plain Mat4 (seam) <-> Diligent float4x4 — both row-major, a straight element copy.
Mat4 ToMat4(const float4x4& m) {
    Mat4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out.m[r * 4 + c] = m[r][c];
    return out;
}
float4x4 ToFloat4x4(const Mat4& in) {
    float4x4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[r][c] = in.m[r * 4 + c];
    return out;
}

} // namespace

void EnsureSceneRoot(Scene& scene) {
    const bool hasRoot = !scene.entities.empty() &&
                         scene.entities[0].parent == -1 &&
                         !scene.entities[0].transform.has_value();
    if (hasRoot) return;

    Entity root;
    root.name   = "Scene";
    root.parent = -1;
    root.transform.reset();   // the root is a pure world anchor (no local transform)
    scene.entities.insert(scene.entities.begin(), std::move(root));

    // Existing entities now sit one slot later: shift their parent indices by +1, and
    // re-parent any previously-rootless entity to the new root at 0.
    for (size_t i = 1; i < scene.entities.size(); ++i) {
        int& p = scene.entities[i].parent;
        p = (p < 0) ? 0 : p + 1;
    }
}

int AddEntity(Scene& scene, int parent, const char* name) {
    const int n = static_cast<int>(scene.entities.size());
    if (parent < 0 || parent >= n) parent = 0;   // default: under the root

    Entity e;
    e.name   = name ? name : "Entity";
    e.parent = parent;
    scene.entities.push_back(std::move(e));
    return static_cast<int>(scene.entities.size()) - 1;
}

void UpdateWorldTransforms(Scene& scene) {
    const int n = static_cast<int>(scene.entities.size());
    for (int i = 0; i < n; ++i) {
        Entity& e = scene.entities[i];
        e.prevWorldMatrix = e.worldMatrix;   // snapshot last frame first (motion vectors)

        // Parent's already-computed world (parents precede children); identity for the root
        // or any entity without a valid earlier parent.
        const float4x4 parentW =
            (e.parent >= 0 && e.parent < i) ? ToFloat4x4(scene.entities[e.parent].worldMatrix)
                                            : float4x4::Identity();
        // Row-vector convention (v' = v·M): apply local first, then the parent.
        const float4x4 local = e.transform ? LocalFromTransform(*e.transform)
                                            : float4x4::Identity();
        e.worldMatrix = ToMat4(local * parentW);
    }
}

} // namespace toon
