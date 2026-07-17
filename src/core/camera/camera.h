#pragma once
//============================================================================
//  core/camera/camera.h — editor camera controls (orbit / pan / zoom / fly / focus).
//
//  Free functions that mutate a toon::Camera (the seam's camera data, from renderer.h)
//  in response to input deltas. Backend-agnostic API; camera.cpp uses Diligent's
//  float4x4 internally to derive the camera's world basis so it matches the renderer's
//  view convention exactly — same build-on-Diligent pattern as scene.cpp.
//============================================================================
#include "core/rendering/renderer.h" // toon::Camera, Vec3

namespace toon {

    // Orbit around the pivot: yaw/pitch by mouse deltas (pixels). Pitch is clamped.
    void CameraOrbit(Camera &cam, float dxPixels, float dyPixels);

    // Pan: slide the pivot (and thus the camera) across the screen plane by mouse deltas.
    void CameraPan(Camera &cam, float dxPixels, float dyPixels);

    // Zoom: move toward/away from the pivot by scroll notches (never through it).
    void CameraZoom(Camera &cam, float scrollNotches);

    // Fly: move the pivot along the camera basis. `fwd`/`right`/`up` are -1..1 axis values
    // (e.g. W/S, D/A, E/Q); `dt` is the frame time in seconds.
    void CameraFly(Camera &cam, float dt, float fwd, float right, float up);

    // Focus: re-target the pivot at a world point (keeps the current distance + orientation).
    void CameraFocus(Camera &cam, const Vec3 &target);

} // namespace toon
