// Scene and entity types.
//
// A Scene holds a flat list of entities. Each entity has a name, transform,
// optional geometry (sub-meshes with materials), and a shading mode that
// tells the renderer which shader pipeline to use.

#pragma once

#include "core/animation.h"
#include "core/animator.h"
#include "core/material.h"
#include "core/mesh.h"
#include "core/texture.h"
#include "core/transform.h"
#include "scene/model_loader.h"

#include <optional>
#include <string>
#include <vector>

enum class ShadingMode {
    VertexColor,  // triangle.vert + triangle.frag (vertex-colored demo geometry)
    Toon,         // model.vert + toon.frag + outline pass
};

// Light entities have no geometry — they contribute to the toon shading pass.
constexpr int kMaxLights = 8;

enum class LightType { Directional, Point };

struct Light {
    LightType type      = LightType::Directional;
    glm::vec3 color{1.0f};
    float     intensity = 1.0f;
    glm::vec3 direction{0.3f, 1.0f, 0.5f};  // directional: toward the light
    float     radius    = 10.0f;             // point: falloff distance
};

struct Entity {
    std::string          name;
    Transform            transform;
    std::vector<SubMesh> subMeshes;
    ShadingMode          shading = ShadingMode::Toon;

    // Skeletal animation (empty skeleton = not skinned).
    Skeleton                   skeleton;
    std::vector<AnimationClip> clips;
    Animator                   animator;
    bool                       skinned = false;

    // Light (nullopt = not a light entity).
    std::optional<Light> light;
};

struct Scene {
    std::vector<Entity> entities;
    int                 selected = -1;  // index into entities, -1 = none
};

// Release all GPU resources owned by every entity in the scene.
inline void DestroyScene(Scene& scene) {
    for (auto& entity : scene.entities) {
        for (auto& sm : entity.subMeshes) {
            DestroyMesh(sm.mesh);
            DestroyTexture(sm.material.texture);
            DestroyTexture(sm.material.normalMap);
        }
    }
    scene.entities.clear();
    scene.selected = -1;
}
