#pragma once
//============================================================================
//  core/math.h — minimal, dependency-free vector types for the engine's public
//  (renderer-seam) API.
//
//  Deliberately NOT Diligent's BasicMath: the renderer seam must not leak any
//  Diligent type (see core/renderer.h), so the vocabulary the engine and game
//  code speak in — vertex positions, light directions, transforms — is defined
//  here in plain structs. renderer.cpp converts these to Diligent's
//  float3/float4/float4x4 internally. Matrices live on the Diligent side of the
//  seam (projection/NDC conventions are backend-specific), so this header
//  intentionally stops at vectors.
//============================================================================
#include <cmath>

namespace toon {

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

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
