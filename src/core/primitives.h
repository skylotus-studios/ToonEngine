#pragma once
//============================================================================
//  core/primitives.h — procedural mesh generators (CPU-side geometry).
//
//  Diligent-free: produces plain toon::Vertex + index arrays that engine/game
//  code hands to Renderer::CreateMesh. Triangles are wound counter-clockwise as
//  seen from OUTSIDE the surface (front faces), matching the fill pass's
//  back-face culling and the outline pass's front-face culling in renderer.cpp.
//============================================================================
#include "core/renderer.h"   // toon::Vertex

#include <cstdint>
#include <vector>

namespace toon {

struct MeshData {
    std::vector<Vertex>   vertices;
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
MeshData MakeTorus(float majorRadius, float minorRadius,
                   uint32_t majorSegments, uint32_t minorSegments);

// Flat square in the XZ plane centered at the origin, facing +Y (a ground plane).
// `halfExtent` is half the side length. Gives SSAO a surface to catch the
// objects' contact shadows.
MeshData MakePlane(float halfExtent);

} // namespace toon
