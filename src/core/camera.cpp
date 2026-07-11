//============================================================================
//  core/camera.cpp — editor camera controls.
//
//  Uses Diligent's float4x4 to derive the camera's world basis from yaw/pitch, so it
//  agrees exactly with the renderer's view (SetCamera). The public API (camera.h) stays
//  Diligent-free — same split as scene.cpp.
//============================================================================
#include "core/camera.h"

#include "BasicMath.hpp"

#include <algorithm>
#include <cmath>

using namespace Diligent;

namespace toon {

namespace {

// The camera's world-space basis. SetCamera builds the view as
//   ... RotationY(yaw) * RotationX(pitch) ...
// so a world direction `a` maps to view space as `a * RotationY(yaw) * RotationX(pitch)`.
// Inverting, the world direction that appears as a given view axis is
//   viewAxis * RotationX(-pitch) * RotationY(-yaw) = the matching ROW of that inverse
// (since e_k * M = row k of M). Deriving it from the same Diligent matrices makes the
// basis correct-by-construction — no hand-guessed left-handed signs.
struct Basis { float3 right, up, forward; };

Basis CameraBasis(const Camera& cam) {
    const float4x4 inv = float4x4::RotationX(-cam.pitch) * float4x4::RotationY(-cam.yaw);
    auto row = [&](int r) { return normalize(float3(inv[r][0], inv[r][1], inv[r][2])); };
    Basis b;
    b.right   = row(0);   // world dir that appears as view +X (screen right)
    b.up      = row(1);   // view +Y (screen up)
    b.forward = row(2);   // view +Z (into the screen, toward the pivot)
    return b;
}

} // namespace

void CameraOrbit(Camera& cam, float dxPixels, float dyPixels) {
    cam.yaw   += dxPixels * cam.lookSensitivity;
    cam.pitch += dyPixels * cam.lookSensitivity;
    cam.pitch  = std::clamp(cam.pitch, -1.55f, 1.55f);   // ~±89 degrees
}

void CameraPan(Camera& cam, float dxPixels, float dyPixels) {
    const Basis b = CameraBasis(cam);
    // Ground-forward: the forward direction flattened onto XZ, so a vertical drag pans
    // across the ground rather than tilting into it.
    float3 groundFwd = float3(b.forward.x, 0.0f, b.forward.z);
    const float glen = length(groundFwd);
    groundFwd = glen > 1e-4f ? groundFwd / glen : b.up;

    // Grab-drag: the point under the cursor follows the cursor (scaled by distance so it
    // feels the same at any zoom).
    const float  scale = cam.distance * cam.panSensitivity;
    const float3 delta = (b.right * (-dxPixels) + groundFwd * dyPixels) * scale;
    cam.pivot.x += delta.x;
    cam.pivot.y += delta.y;
    cam.pivot.z += delta.z;
}

void CameraZoom(Camera& cam, float scrollNotches) {
    // Geometric zoom: each notch scales the distance, so it feels even across the range.
    cam.distance *= std::pow(1.0f - cam.zoomSpeed, scrollNotches);
    cam.distance  = std::max(cam.distance, 0.05f);
}

void CameraFly(Camera& cam, float dt, float fwd, float right, float up) {
    if (fwd == 0.0f && right == 0.0f && up == 0.0f) return;
    const Basis b = CameraBasis(cam);
    float3 move = b.forward * fwd + b.right * right;
    move.y += up;                          // up/down is world-space (E/Q)
    const float len = length(move);
    if (len < 1e-4f) return;
    move = (move / len) * (cam.moveSpeed * dt);
    cam.pivot.x += move.x;
    cam.pivot.y += move.y;
    cam.pivot.z += move.z;
}

void CameraFocus(Camera& cam, const Vec3& target) {
    cam.pivot = target;
}

} // namespace toon
