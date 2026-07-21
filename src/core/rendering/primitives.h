#pragma once
//============================================================================
//  core/rendering/primitives.h: procedural mesh generators (CPU-side geometry).
//
//  Diligent-free: produces plain toon::Vertex + index arrays that engine/game
//  code hands to Renderer::CreateMesh. Triangles are wound counter-clockwise as
//  seen from OUTSIDE the surface (front faces), matching the fill pass's
//  back-face culling and the outline pass's front-face culling in renderer.cpp.
//============================================================================
#include "core/rendering/renderer.h" // toon::Vertex

#include <cstdint>
#include <vector>

namespace toon {

    struct MeshData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    // UV sphere with smooth (per-vertex) normals. Smooth shading + a clean outline.
    MeshData MakeUVSphere(float radius, uint32_t rings, uint32_t segments);

    // Axis-aligned cube (24 verts). `normal` is per-face for crisp flat toon
    // shading; `smoothNormal` points from the center through each corner so the
    // three verts sharing a corner extrude to the same point and the outline stays
    // closed. `halfExtent` is half the side length.
    MeshData MakeCube(float halfExtent);

    // Torus in the XZ plane. Smooth normals; a nice curved surface for the banded
    // ramp. `majorRadius` is the ring radius, `minorRadius` the tube radius.
    MeshData MakeTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments);

    // Flat square in the XZ plane centered at the origin, facing +Y (a ground plane).
    // `halfExtent` is half the side length. Gives SSAO a surface to catch the
    // objects' contact shadows.
    MeshData MakePlane(float halfExtent);

    // --- Primitive provenance (for scene serialization) -------------------------
    // A procedural mesh has no source file to reload from, unlike a loaded glTF model, so a
    // saved scene instead records which generator above built it, plus its params, and
    // regenerates via MakePrimitiveMesh on load. See core/scene/serializer.h and Entity::primitive.

    enum class PrimitiveKind : uint8_t { None, Sphere, Cube, Torus, Plane };

    struct PrimitiveDesc {
        PrimitiveKind kind = PrimitiveKind::None;
        float radius = 1.0f;       // Sphere radius, or Torus majorRadius
        float minorRadius = 0.35f; // Torus tube radius
        float halfExtent = 1.0f;   // Cube / Plane
        uint32_t segmentsA = 32;   // Sphere rings, or Torus majorSegments
        uint32_t segmentsB = 48;   // Sphere segments, or Torus minorSegments

        static PrimitiveDesc Sphere(float radius, uint32_t rings, uint32_t segments);
        static PrimitiveDesc Cube(float halfExtent);
        static PrimitiveDesc Torus(float majorRadius, float minorRadius, uint32_t majorSegments,
                                   uint32_t minorSegments);
        static PrimitiveDesc Plane(float halfExtent);
    };

    // Dispatch to the matching MakeXxx generator above from a PrimitiveDesc (PrimitiveKind::None
    // returns an empty MeshData: nothing to draw).
    MeshData MakePrimitiveMesh(const PrimitiveDesc &desc);

} // namespace toon
