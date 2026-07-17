//============================================================================
//  core/rendering/primitives.cpp — procedural mesh generators.
//
//  Triangles are wound counter-clockwise as seen from OUTSIDE the surface (see
//  primitives.h). Every vertex carries a per-face `normal` (fill shading) and a
//  `smoothNormal` (outline hull extrusion) — identical for smooth meshes.
//============================================================================
#include "core/rendering/primitives.h"

#include <cmath>

namespace toon {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTau = 2.0f * kPi;

        // Append two CCW triangles for the quad (a, b, c, d).
        void AddQuad(std::vector<uint32_t> &indices, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    } // namespace

    MeshData MakeUVSphere(float radius, uint32_t rings, uint32_t segments) {
        if (rings < 2) { rings = 2; }
        if (segments < 3) { segments = 3; }

        MeshData mesh;
        // (rings+1) latitude bands x (segments+1) longitude columns; the last column
        // duplicates the first so a future UV seam wraps cleanly.
        for (uint32_t i = 0; i <= rings; ++i) {
            const float phi = (static_cast<float>(i) / rings) * kPi; // 0 at +Y pole -> kPi at -Y pole
            const float y = std::cos(phi);
            const float r = std::sin(phi);
            for (uint32_t j = 0; j <= segments; ++j) {
                const float theta = (static_cast<float>(j) / segments) * kTau;
                const Vec3 n{r * std::cos(theta), y, r * std::sin(theta)}; // unit position == smooth normal
                mesh.vertices.push_back(Vertex{n * radius, n, n});
            }
        }

        const uint32_t stride = segments + 1;
        mesh.indices.reserve(static_cast<size_t>(rings) * segments * 6);
        for (uint32_t i = 0; i < rings; ++i) {
            for (uint32_t j = 0; j < segments; ++j) {
                const uint32_t a = i * stride + j;
                AddQuad(mesh.indices, a, a + stride, a + stride + 1, a + 1);
            }
        }
        return mesh;
    }

    MeshData MakeCube(float halfExtent) {
        const float h = halfExtent;

        // Per face: outward normal n and in-plane axes u, v chosen so u x v = n.
        // Corners are wound (-u-v, -u+v, +u+v, +u-v): this is CCW in screen for the
        // viewer-facing face under Diligent's LEFT-handed projection (the reverse of
        // the right-handed u x v = n order) — matching the sphere/torus convention
        // and the PSOs' FrontCounterClockwise = True.
        struct Face {
            Vec3 n, u, v;
        };
        const Face faces[6] = {
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  // +X
            {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, // -X
            {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  // +Y
            {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, // -Y
            {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  // +Z
            {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}, // -Z
        };

        MeshData mesh;
        mesh.vertices.reserve(24);
        mesh.indices.reserve(36);
        for (const Face &f : faces) {
            const Vec3 c = f.n * h;
            const Vec3 uu = f.u * h;
            const Vec3 vv = f.v * h;
            const Vec3 corners[4] = {c - uu - vv, c - uu + vv, c + uu + vv, c + uu - vv};
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            for (const Vec3 &p : corners) {
                mesh.vertices.push_back(Vertex{p, f.n, Normalize(p)}); // smoothNormal points corner-outward
            }
            AddQuad(mesh.indices, base, base + 1, base + 2, base + 3);
        }
        return mesh;
    }

    MeshData MakeTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments) {
        if (majorSegments < 3) { majorSegments = 3; }
        if (minorSegments < 3) { minorSegments = 3; }

        MeshData mesh;
        const Vec3 up{0.0f, 1.0f, 0.0f};
        // i sweeps around the big ring (theta); j sweeps around the tube (phi).
        for (uint32_t i = 0; i <= majorSegments; ++i) {
            const float theta = (static_cast<float>(i) / majorSegments) * kTau;
            const Vec3 ringDir{std::cos(theta), 0.0f, std::sin(theta)};
            const Vec3 center = ringDir * majorRadius;
            for (uint32_t j = 0; j <= minorSegments; ++j) {
                const float phi = (static_cast<float>(j) / minorSegments) * kTau;
                const Vec3 n = ringDir * std::cos(phi) + up * std::sin(phi); // unit tube normal
                mesh.vertices.push_back(Vertex{center + n * minorRadius, n, n});
            }
        }

        const uint32_t stride = minorSegments + 1;
        mesh.indices.reserve(static_cast<size_t>(majorSegments) * minorSegments * 6);
        for (uint32_t i = 0; i < majorSegments; ++i) {
            for (uint32_t j = 0; j < minorSegments; ++j) {
                const uint32_t a = i * stride + j;
                AddQuad(mesh.indices, a, a + stride, a + stride + 1, a + 1);
            }
        }
        return mesh;
    }

    MeshData MakePlane(float halfExtent) {
        const float h = halfExtent;
        const Vec3 n{0.0f, 1.0f, 0.0f}; // faces +Y (up)

        // Wound to match the +Y face of MakeCube (CCW seen from above under the
        // left-handed projection), so it survives the fill's back-face culling.
        //
        // smoothNormal drives the outline pass's hull-extrude direction, not shading -- and here
        // it deliberately ISN'T `n`. A flat quad's true normal is identical at all four corners, so
        // extruding along it (like the curved primitives do) only lifts the whole quad toward the
        // camera instead of widening its silhouette, and the "outline" ends up covering the entire
        // face rather than just its border. Pointing it outward in-plane (center-through-corner,
        // the same idea as MakeCube's corner bulge) grows the hull sideways instead, so only a
        // border rim survives past the fill's edges.
        const Vec3 out[4] = {
            Normalize(Vec3{-1.0f, 0.0f, -1.0f}),
            Normalize(Vec3{-1.0f, 0.0f, 1.0f}),
            Normalize(Vec3{1.0f, 0.0f, 1.0f}),
            Normalize(Vec3{1.0f, 0.0f, -1.0f}),
        };
        MeshData mesh;
        mesh.vertices = {
            Vertex{{-h, 0.0f, -h}, n, out[0]},
            Vertex{{-h, 0.0f, h}, n, out[1]},
            Vertex{{h, 0.0f, h}, n, out[2]},
            Vertex{{h, 0.0f, -h}, n, out[3]},
        };
        AddQuad(mesh.indices, 0, 1, 2, 3);
        return mesh;
    }

    // --- Primitive provenance -----------------------------------------------------

    PrimitiveDesc PrimitiveDesc::Sphere(float radius, uint32_t rings, uint32_t segments) {
        PrimitiveDesc d;
        d.kind = PrimitiveKind::Sphere;
        d.radius = radius;
        d.segmentsA = rings;
        d.segmentsB = segments;
        return d;
    }

    PrimitiveDesc PrimitiveDesc::Cube(float halfExtent) {
        PrimitiveDesc d;
        d.kind = PrimitiveKind::Cube;
        d.halfExtent = halfExtent;
        return d;
    }

    PrimitiveDesc PrimitiveDesc::Torus(float majorRadius, float minorRadius, uint32_t majorSegments,
                                       uint32_t minorSegments) {
        PrimitiveDesc d;
        d.kind = PrimitiveKind::Torus;
        d.radius = majorRadius;
        d.minorRadius = minorRadius;
        d.segmentsA = majorSegments;
        d.segmentsB = minorSegments;
        return d;
    }

    PrimitiveDesc PrimitiveDesc::Plane(float halfExtent) {
        PrimitiveDesc d;
        d.kind = PrimitiveKind::Plane;
        d.halfExtent = halfExtent;
        return d;
    }

    // Dispatches to the MakeXxx generator `kind` names; unpacks the shared param slots back into
    // each generator's own argument names (see PrimitiveDesc's field comments for the mapping).
    MeshData MakePrimitiveMesh(const PrimitiveDesc &desc) {
        switch (desc.kind) {
            case PrimitiveKind::Sphere:
                return MakeUVSphere(desc.radius, desc.segmentsA, desc.segmentsB);
            case PrimitiveKind::Cube:
                return MakeCube(desc.halfExtent);
            case PrimitiveKind::Torus:
                return MakeTorus(desc.radius, desc.minorRadius, desc.segmentsA, desc.segmentsB);
            case PrimitiveKind::Plane:
                return MakePlane(desc.halfExtent);
            case PrimitiveKind::None:
                break;
        }
        return MeshData{};
    }

} // namespace toon
