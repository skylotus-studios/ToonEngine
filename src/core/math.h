#pragma once
//============================================================================
//  core/math.h — minimal, dependency-free vector types for the engine's public
//  (renderer-seam) API.
//
//  Deliberately NOT Diligent's BasicMath: the renderer seam must not leak any
//  Diligent type (see core/renderer.h), so the vocabulary the engine and game
//  code speak in — vertex positions, light directions, transforms — is defined
//  here in plain structs. renderer.cpp converts these to Diligent's
//  float3/float4/float4x4 internally. Projection/view matrices (NDC + handedness
//  conventions are backend-specific) stay behind the seam; the one matrix here is a
//  plain, math-free `Mat4` — the vocabulary for a composed world transform the scene
//  graph hands to the renderer (the actual 4x4 math happens on the Diligent side).
//============================================================================
#include <cmath>

namespace toon {

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

// A 4x4 matrix as *pure data* — the seam's vocabulary for a composed world transform
// (object -> world). Row-major, matching Diligent's float4x4 memory layout, so the
// renderer converts by a straight copy. Intentionally math-free: composition/inverse
// happen on the Diligent side (core/scene.cpp, core/renderer.cpp). Defaults to identity.
struct Mat4 {
    float m[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f };
    static Mat4 Identity() { return Mat4{}; }
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(const Vec3& v, float s)       { return { v.x * s, v.y * s, v.z * s }; }

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(const Vec3& v)             { return std::sqrt(Dot(v, v)); }

inline Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

} // namespace toon
