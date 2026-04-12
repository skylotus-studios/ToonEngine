// Model loading for glTF (.gltf/.glb) and FBX (.fbx) formats.
//
// Dispatches to cgltf or ufbx based on file extension. Returns GPU-ready
// meshes, materials, skeleton, and animation clips.
//
// Vertex layout for loaded models:
//   Non-skinned: position(vec3) + normal(vec3) + texcoord(vec2), stride 32
//   Skinned:     + boneIds(vec4) + boneWeights(vec4), stride 56

#pragma once

#include "core/animation.h"
#include "core/material.h"
#include "core/mesh.h"

#include <glm/glm.hpp>

#include <vector>

struct SubMesh {
    Mesh     mesh;
    Material material;
};

struct LoadedModel {
    std::vector<SubMesh>       subMeshes;
    glm::vec3                  boundsMin{0.0f};
    glm::vec3                  boundsMax{0.0f};
    Skeleton                   skeleton;
    std::vector<AnimationClip> clips;
    bool                       skinned = false;
};

// Load a 3D model from disk. Supported formats: .gltf, .glb, .fbx.
// Returns sub-meshes with materials, skeleton, animation clips, and
// bounding box. Empty subMeshes on failure.
LoadedModel LoadModel(const char* path);
