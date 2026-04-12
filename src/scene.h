// Scene and entity types.
//
// A Scene holds a flat list of entities. Each entity has a name, transform,
// optional geometry (sub-meshes with materials), and a shading mode that
// tells the renderer which shader pipeline to use.

#pragma once

#include "material.h"
#include "mesh.h"
#include "model_loader.h"
#include "texture.h"
#include "transform.h"

#include <string>
#include <vector>

enum class ShadingMode {
    VertexColor,  // triangle.vert + triangle.frag (vertex-colored demo geometry)
    Toon,         // model.vert + toon.frag + outline pass
};

struct Entity {
    std::string          name;
    Transform            transform;
    std::vector<SubMesh> subMeshes;
    ShadingMode          shading = ShadingMode::Toon;
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
        }
    }
    scene.entities.clear();
    scene.selected = -1;
}
