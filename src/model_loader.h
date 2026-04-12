// Model loading for glTF (.gltf/.glb) and FBX (.fbx) formats.
//
// Dispatches to cgltf or ufbx based on file extension. Returns GPU-ready
// meshes with a fixed vertex layout: position (vec3) + normal (vec3) +
// texcoord (vec2). Missing attributes are filled with defaults
// (normal = 0,0,1; texcoord = 0,0).

#pragma once

#include "mesh.h"

#include <glm/glm.hpp>

#include <vector>

// Result of loading a model file.
struct LoadedModel {
    std::vector<Mesh> meshes;
    glm::vec3 boundsMin{0.0f};  // axis-aligned bounding box minimum
    glm::vec3 boundsMax{0.0f};  // axis-aligned bounding box maximum
};

// Load a 3D model from disk. Supported formats: .gltf, .glb, .fbx.
// Returns meshes + bounding box. meshes is empty on failure.
LoadedModel LoadModel(const char* path);
