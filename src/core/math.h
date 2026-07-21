#pragma once
//============================================================================
//  core/math.h: minimal, dependency-free vector types for the engine's public
//  (renderer-seam) API.
//
//  Deliberately NOT Diligent's BasicMath: the renderer seam must not leak any
//  Diligent type (see core/renderer.h), so the vocabulary the engine and game
//  code speak in (vertex positions, light directions, transforms) is defined
//  here in plain structs. renderer.cpp converts these to Diligent's
//  float3/float4/float4x4 internally. Projection/view matrices (NDC + handedness
//  conventions are backend-specific) stay behind the seam; the one matrix here is a
//  plain, math-free `Mat4`: the vocabulary for a composed world transform the scene
//  graph hands to the renderer (the actual 4x4 math happens on the Diligent side).
//============================================================================
#include <cmath>

namespace toon {

    struct Vec2 {
        float x = 0.0f, y = 0.0f;
    };
    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    struct Vec4 {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    };

    // A rotation as a unit quaternion (x,y,z = imaginary/vector part, w = real/scalar part).
    // Defaults to identity (no rotation). Plain data, like Mat4 below: the seam's vocabulary
    // for "an entity's orientation" (Transform::rotation, core/renderer.h). Composition/matrix
    // conversion/spherical interpolation are numerically fiddly enough that they're built on
    // Diligent's own Quaternion<T> (core/scene.cpp, core/renderer.cpp), per the guiding
    // principle, NOT reimplemented here. The handful of operations below ARE hand-rolled
    // anyway, for the same reason Vec3's Dot/Length/Normalize are: gameplay scripts
    // (core/scripts/*, e.g. SpinScript) and the app layer (main.cpp's Inspector) must stay
    // Diligent-free, so they need a way to compose/convert a rotation without including
    // BasicMath.hpp. Kept deliberately small: just enough for those two call sites.
    struct Quat {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    };

    // A 4x4 matrix as *pure data*: the seam's vocabulary for a composed world transform
    // (object -> world). Row-major, matching Diligent's float4x4 memory layout, so the
    // renderer converts by a straight copy. Intentionally math-free: composition/inverse
    // happen on the Diligent side (core/scene.cpp, core/renderer.cpp). Defaults to identity.
    struct Mat4 {
        float m[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        static Mat4 Identity() { return Mat4{}; }
    };

    inline Vec3 operator+(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline Vec3 operator-(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline Vec3 operator*(const Vec3 &v, float s) { return {v.x * s, v.y * s, v.z * s}; }

    inline float Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Length(const Vec3 &v) { return std::sqrt(Dot(v, v)); }

    inline Vec3 Normalize(const Vec3 &v) {
        const float len = Length(v);
        return len > 0.0f ? v * (1.0f / len) : v;
    }

    // --- Quat: hand-rolled, dependency-free (see the struct's own comment for why) ---------

    // Hamilton product, mirroring Diligent::Quaternion<T>::Mul field-for-field so a Quat
    // composed here means exactly what Diligent's own Mul would once converted to a
    // QuaternionF (core/scene.cpp). `a * b` applies `b`'s rotation first, `a`'s second/last:
    // e.g. to pre-multiply a world-space delta onto an existing rotation, write `delta * old`.
    inline Quat operator*(const Quat &a, const Quat &b) {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    inline float Dot(const Quat &a, const Quat &b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    inline float Length(const Quat &q) { return std::sqrt(Dot(q, q)); }

    inline Quat Normalize(const Quat &q) {
        const float len = Length(q);
        if (len <= 0.0f) { return q; }
        const float inv = 1.0f / len;
        return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    }

    // A rotation of `angleRadians` around `axis` (need not be pre-normalized). Mirrors
    // Diligent::Quaternion<T>::RotationFromAxisAngle exactly, including its degenerate-axis
    // convention (a zero-length axis yields identity, ignoring the angle).
    inline Quat QuatFromAxisAngle(const Vec3 &axis, float angleRadians) {
        const float len = Length(axis);
        if (len <= 0.0f) { return Quat{}; }
        const Vec3 a = axis * (1.0f / len);
        const float half = angleRadians * 0.5f;
        const float s = std::sin(half);
        return {a.x * s, a.y * s, a.z * s, std::cos(half)};
    }

    // Euler XYZ (radians) -> quaternion, applying X, then Y, then Z (matches
    // core/scene.cpp's LocalFromTransform / renderer.cpp's WorldFromTransform composition
    // order). The inspector (main.cpp) is the one call site: it edits rotation as Euler
    // degrees, converting to/from a quaternion at the widget boundary. Composed as
    // `qz * qy * qx`, per operator*'s "b first, a last" rule, so that applies qx first, qy
    // second, qz last, i.e. X then Y then Z.
    inline Quat QuatFromEuler(const Vec3 &eulerRadians) {
        const Quat qx = QuatFromAxisAngle({1.0f, 0.0f, 0.0f}, eulerRadians.x);
        const Quat qy = QuatFromAxisAngle({0.0f, 1.0f, 0.0f}, eulerRadians.y);
        const Quat qz = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, eulerRadians.z);
        return Normalize(qz * qy * qx);
    }

    // The inverse of QuatFromEuler: quaternion -> Euler XYZ (radians). Ports
    // core/scene.cpp's DecomposeToTransform derivation (see that function's comment for the
    // R = Rx(a)*Ry(b)*Rz(g) algebra this solves) onto quaternion-derived matrix entries
    // instead of Diligent float3 rows, so the app layer never needs BasicMath.hpp just to
    // show a rotation in the Inspector. Same gimbal-lock (pitch ~ +/-90 deg) fallback.
    inline Vec3 QuatToEuler(const Quat &q) {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float r02 = 2.0f * q.x * q.z - 2.0f * q.w * q.y; // R[0][2] = -sin(b)
        const float r00 = 1.0f - 2.0f * yy - 2.0f * zz;        // R[0][0] = cos(b)*cos(g)
        const float r01 = 2.0f * q.x * q.y + 2.0f * q.w * q.z; // R[0][1] = cos(b)*sin(g)
        const float r12 = 2.0f * q.y * q.z + 2.0f * q.w * q.x; // R[1][2] = sin(a)*cos(b)
        const float r22 = 1.0f - 2.0f * xx - 2.0f * yy;        // R[2][2] = cos(a)*cos(b)
        const float r10 = 2.0f * q.x * q.y - 2.0f * q.w * q.z; // R[1][0]
        const float r11 = 1.0f - 2.0f * xx - 2.0f * zz;        // R[1][1]

        const float sinB = r02 < -1.0f ? 1.0f : (r02 > 1.0f ? -1.0f : -r02);
        const float b = std::asin(sinB);
        const float cosB = std::cos(b);
        float a, g;
        if (std::abs(cosB) > 1e-4f) {
            a = std::atan2(r12, r22);
            g = std::atan2(r01, r00);
        } else {
            // Gimbal lock (cos b ~ 0, pitch ~ +/-90 deg): a and g are coupled -- pin g = 0.
            g = 0.0f;
            a = (r02 < 0.0f) ? std::atan2(r10, r11) : std::atan2(-r10, r11);
        }
        return {a, b, g};
    }

} // namespace toon
