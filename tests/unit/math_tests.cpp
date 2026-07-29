//============================================================================
//  tests/unit/math_tests.cpp: pure-function coverage of core/math.h.
//
//  Header-only, no device, no RuntimeState -- these are the fastest, most isolated tests this
//  suite has, which is why they're the ones the fast tier's `unit_tests` step runs (CTest
//  filter "Math"; see CMakeLists.txt's ToonUnitTests_Math entry).
//============================================================================
#include "core/math.h"
#include "test_framework.h"

using namespace toon;

TOON_TEST("Math.Vec3.AddSub") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, -1.0f, 0.5f};
    const Vec3 sum = a + b;
    CHECK_NEAR(sum.x, 5.0f, 1e-6);
    CHECK_NEAR(sum.y, 1.0f, 1e-6);
    CHECK_NEAR(sum.z, 3.5f, 1e-6);

    const Vec3 diff = a - b;
    CHECK_NEAR(diff.x, -3.0f, 1e-6);
    CHECK_NEAR(diff.y, 3.0f, 1e-6);
    CHECK_NEAR(diff.z, 2.5f, 1e-6);
}

TOON_TEST("Math.Vec3.DotLengthNormalize") {
    const Vec3 v{3.0f, 4.0f, 0.0f};
    CHECK_NEAR(Dot(v, v), 25.0f, 1e-6);
    CHECK_NEAR(Length(v), 5.0f, 1e-6);

    const Vec3 n = Normalize(v);
    CHECK_NEAR(Length(n), 1.0f, 1e-5);
    CHECK_NEAR(n.x, 0.6f, 1e-5);
    CHECK_NEAR(n.y, 0.8f, 1e-5);

    // Normalize's own degenerate-input convention: a zero vector passes through unchanged
    // rather than dividing by zero.
    const Vec3 zero = Normalize(Vec3{0.0f, 0.0f, 0.0f});
    CHECK_NEAR(zero.x, 0.0f, 1e-6);
    CHECK_NEAR(zero.y, 0.0f, 1e-6);
    CHECK_NEAR(zero.z, 0.0f, 1e-6);
}

TOON_TEST("Math.Quat.MultiplyIdentity") {
    const Quat identity{};
    const Quat q = QuatFromAxisAngle({0.0f, 1.0f, 0.0f}, 0.7f);
    const Quat r1 = identity * q;
    const Quat r2 = q * identity;
    CHECK_NEAR(r1.x, q.x, 1e-6);
    CHECK_NEAR(r1.y, q.y, 1e-6);
    CHECK_NEAR(r1.z, q.z, 1e-6);
    CHECK_NEAR(r1.w, q.w, 1e-6);
    CHECK_NEAR(r2.x, q.x, 1e-6);
    CHECK_NEAR(r2.y, q.y, 1e-6);
    CHECK_NEAR(r2.z, q.z, 1e-6);
    CHECK_NEAR(r2.w, q.w, 1e-6);
}

TOON_TEST("Math.Quat.Normalize") {
    const Quat q{2.0f, 0.0f, 0.0f, 0.0f}; // unnormalized, length 2
    const Quat n = Normalize(q);
    CHECK_NEAR(Length(n), 1.0f, 1e-5);
    CHECK_NEAR(n.x, 1.0f, 1e-5);

    // Normalize's own degenerate-input convention (mirrors Vec3's): a zero-length quat passes
    // through unchanged.
    const Quat zero{0.0f, 0.0f, 0.0f, 0.0f};
    const Quat zn = Normalize(zero);
    CHECK_NEAR(zn.w, 0.0f, 1e-6);
}

TOON_TEST("Math.Quat.FromAxisAngleRotatesExpectedDirection") {
    // A 90-degree rotation around +Z should send +X to +Y (right-handed convention, matching
    // Diligent::Quaternion<T>::RotationFromAxisAngle -- see QuatFromAxisAngle's own comment).
    const Quat q = QuatFromAxisAngle({0.0f, 0.0f, 1.0f}, 1.57079633f); // pi/2
    // Rotate {1,0,0} by q via q * v * q^-1, hand-expanded for a pure quat (w=0) rotation --
    // simplest to just check q's own components against the closed-form 90-degree quat
    // (sin(pi/4), 0, 0, cos(pi/4)) on the Z axis rather than re-deriving quaternion rotation
    // here (that machinery deliberately stays on the Diligent side; see core/math.h's banner).
    CHECK_NEAR(q.x, 0.0f, 1e-5);
    CHECK_NEAR(q.y, 0.0f, 1e-5);
    CHECK_NEAR(q.z, 0.70710678f, 1e-5);
    CHECK_NEAR(q.w, 0.70710678f, 1e-5);

    // Degenerate axis: QuatFromAxisAngle's own documented convention is identity, ignoring the
    // angle, matching Diligent::Quaternion<T>::RotationFromAxisAngle.
    const Quat degenerate = QuatFromAxisAngle({0.0f, 0.0f, 0.0f}, 1.0f);
    CHECK_NEAR(degenerate.x, 0.0f, 1e-6);
    CHECK_NEAR(degenerate.y, 0.0f, 1e-6);
    CHECK_NEAR(degenerate.z, 0.0f, 1e-6);
    CHECK_NEAR(degenerate.w, 1.0f, 1e-6);
}

TOON_TEST("Math.Quat.EulerRoundTrip") {
    // Well away from the pitch = +/-90deg gimbal-lock fallback QuatToEuler's own comment names.
    const Vec3 original{0.30f, -0.50f, 0.70f};
    const Quat q = QuatFromEuler(original);
    const Vec3 recovered = QuatToEuler(q);
    CHECK_NEAR(recovered.x, original.x, 1e-4);
    CHECK_NEAR(recovered.y, original.y, 1e-4);
    CHECK_NEAR(recovered.z, original.z, 1e-4);

    // The round trip's OWN closure: converting back to a quat again must match the first quat
    // (not just the Euler angles, which is the weaker property -- two different quats can
    // decompose to Euler angles that happen to re-encode close, this checks the actual
    // rotation matches).
    const Quat q2 = QuatFromEuler(recovered);
    CHECK_NEAR(Dot(q, q2), 1.0f, 1e-4); // unit quats representing the same rotation: dot == 1 (or -1)
}
