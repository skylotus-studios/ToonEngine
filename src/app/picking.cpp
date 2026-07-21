//============================================================================
//  app/picking.cpp: see picking.h.
//============================================================================
#include "app/picking.h"

#include "app/editor_state.h"

namespace toon {

    namespace {

        struct AABB {
            Vec3 min;
            Vec3 max;
        };

        // Row-vector transform (v' = v * M), matching Mat4's convention (renderer.cpp's
        // TransformRowVector does the same thing for Diligent's float4x4; Mat4 is that same
        // row-major layout copied straight across the seam -- see core/math.h).
        Vec3 TransformPoint(const Vec3 &p, const Mat4 &m) {
            return {
                p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12],
                p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13],
                p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10] + m.m[14],
            };
        }

        // An entity's world-space pick bounds. Mesh/model entities transform their local AABB's
        // 8 corners by worldMatrix and re-derive min/max (a rotated box isn't just its two
        // corners transformed); everything else falls back to a fixed box around its world
        // position, so lights/empty anchors stay clickable (see picking.h's kPickBoxHalfExtent).
        AABB EntityWorldBounds(const Entity &e, const Renderer &renderer) {
            Vec3 localMin, localMax;
            bool hasBounds = false;
            if (e.mesh != MeshHandle::Invalid) {
                hasBounds = renderer.GetMeshBounds(e.mesh, localMin, localMax);
            } else if (e.model != ModelHandle::Invalid) {
                hasBounds = renderer.GetModelBounds(e.model, localMin, localMax);
            }

            if (!hasBounds) {
                const Vec3 center = TransformPoint({0.0f, 0.0f, 0.0f}, e.worldMatrix);
                const Vec3 half{kPickBoxHalfExtent, kPickBoxHalfExtent, kPickBoxHalfExtent};
                return {center - half, center + half};
            }

            const Vec3 corners[8] = {
                {localMin.x, localMin.y, localMin.z}, {localMax.x, localMin.y, localMin.z},
                {localMin.x, localMax.y, localMin.z}, {localMax.x, localMax.y, localMin.z},
                {localMin.x, localMin.y, localMax.z}, {localMax.x, localMin.y, localMax.z},
                {localMin.x, localMax.y, localMax.z}, {localMax.x, localMax.y, localMax.z},
            };
            Vec3 worldMin = TransformPoint(corners[0], e.worldMatrix);
            Vec3 worldMax = worldMin;
            for (int i = 1; i < 8; ++i) {
                const Vec3 p = TransformPoint(corners[i], e.worldMatrix);
                if (p.x < worldMin.x) { worldMin.x = p.x; }
                if (p.y < worldMin.y) { worldMin.y = p.y; }
                if (p.z < worldMin.z) { worldMin.z = p.z; }
                if (p.x > worldMax.x) { worldMax.x = p.x; }
                if (p.y > worldMax.y) { worldMax.y = p.y; }
                if (p.z > worldMax.z) { worldMax.z = p.z; }
            }
            return {worldMin, worldMax};
        }

        // Standard slab test. `direction` is a unit vector here (unlike PhysicsWorld::Raycast's
        // length-encoding convention) -- picking rays aren't clipped to a max distance, since a
        // click should be able to select something past the camera's farZ. outT is the entry
        // distance along the ray on a hit.
        bool RayIntersectsAABB(const Vec3 &origin, const Vec3 &dir, const AABB &box, float &outT) {
            float tMin = 0.0f;
            float tMax = 3.402823e37f; // FLT_MAX -- no far clip for a pick ray

            const float o[3] = {origin.x, origin.y, origin.z};
            const float d[3] = {dir.x, dir.y, dir.z};
            const float bmin[3] = {box.min.x, box.min.y, box.min.z};
            const float bmax[3] = {box.max.x, box.max.y, box.max.z};

            for (int axis = 0; axis < 3; ++axis) {
                if (d[axis] > -1e-8f && d[axis] < 1e-8f) {
                    if (o[axis] < bmin[axis] || o[axis] > bmax[axis]) { return false; }
                    continue;
                }
                float t1 = (bmin[axis] - o[axis]) / d[axis];
                float t2 = (bmax[axis] - o[axis]) / d[axis];
                if (t1 > t2) {
                    const float tmp = t1;
                    t1 = t2;
                    t2 = tmp;
                }
                if (t1 > tMin) { tMin = t1; }
                if (t2 < tMax) { tMax = t2; }
                if (tMin > tMax) { return false; }
            }
            outT = tMin;
            return true;
        }

    } // namespace

    int PickEntity(const Scene &scene, const Renderer &renderer, const Vec3 &rayOrigin, const Vec3 &rayDir) {
        int bestIdx = -1;
        float bestT = 0.0f;
        // Index 0 is the implicit root (never renderable, never worth picking) -- see scene.h.
        for (int i = 1; i < static_cast<int>(scene.entities.size()); ++i) {
            const Entity &e = scene.entities[i];
            if (!e.transform) { continue; } // an anchor with no local placement has no world pose

            float t = 0.0f;
            if (RayIntersectsAABB(rayOrigin, rayDir, EntityWorldBounds(e, renderer), t) &&
                (bestIdx == -1 || t < bestT)) {
                bestIdx = i;
                bestT = t;
            }
        }
        return bestIdx;
    }

    void DoMousePicking(EditorState &state) {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { return; }
        if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { return; }

        // A pick is a CLICK, not a drag -- ImGui's own per-button drag delta (accumulated since
        // the button went down) tells the two apart. A real left-button drag over empty space
        // is reserved for a future box-select; it isn't read here.
        constexpr float kClickMaxDragPx = 4.0f;
        const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if ((drag.x * drag.x + drag.y * drag.y) > (kClickMaxDragPx * kClickMaxDragPx)) { return; }

        Vec3 rayOrigin, rayDir;
        state.renderer.ScreenPointToRay(io.MousePos.x, io.MousePos.y, io.DisplaySize.x, io.DisplaySize.y, rayOrigin,
                                        rayDir);
        state.scene.selected = PickEntity(state.scene, state.renderer, rayOrigin, rayDir);
    }

} // namespace toon
