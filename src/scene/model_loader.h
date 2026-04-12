// Model loading for glTF (.gltf/.glb) and FBX (.fbx) formats.
//
// Dispatches to cgltf or ufbx based on file extension. Returns GPU-ready
// meshes with a fixed vertex layout: position (vec3) + normal (vec3) +
// texcoord (vec2). Each sub-mesh carries its own Material (base color +
// optional texture extracted from the source file).

#pragma once

#include "core/material.h"
#include "core/mesh.h"

#include <glm/glm.hpp>

#include <vector>

struct SubMesh {
    Mesh     mesh;
    Material material;
};

struct LoadedModel {
    std::vector<SubMesh> subMeshes;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// Load a 3D model from disk. Supported formats: .gltf, .glb, .fbx.
// Returns sub-meshes with materials + bounding box. Empty on failure.
LoadedModel LoadModel(const char* path);
