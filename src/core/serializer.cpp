//============================================================================
//  core/serializer.cpp — scene save/load implementation.
//
//  A simple line-based text format: "key value..." per line, two-space-indented entity
//  properties following an "entity "Name"" header, a blank line between entities. Ported
//  from ToonEngineOld/src/scene/serializer.cpp, generalized where this engine's Entity
//  differs (procedural primitives alongside loaded models; camera is orbit-style).
//============================================================================
#include "core/serializer.h"

#include "core/primitives.h"
#include "core/script.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace toon {

namespace {

// --- Write helpers ------------------------------------------------------------

void WriteVec3(std::ofstream& f, const char* key, const Vec3& v) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s %.6f %.6f %.6f\n", key, v.x, v.y, v.z);
    f << buf;
}

void WriteFloat(std::ofstream& f, const char* key, float v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %.6f\n", key, v);
    f << buf;
}

// One line per PrimitiveKind, since each generator takes a different param list (see
// PrimitiveDesc's field comments in primitives.h for the kind -> field mapping).
void WritePrimitive(std::ofstream& f, const PrimitiveDesc& d) {
    char buf[192];
    switch (d.kind) {
        case PrimitiveKind::Sphere:
            std::snprintf(buf, sizeof(buf), "  primitive sphere %.6f %u %u\n", d.radius, d.segmentsA, d.segmentsB);
            break;
        case PrimitiveKind::Cube:
            std::snprintf(buf, sizeof(buf), "  primitive cube %.6f\n", d.halfExtent);
            break;
        case PrimitiveKind::Torus:
            std::snprintf(buf, sizeof(buf), "  primitive torus %.6f %.6f %u %u\n", d.radius, d.minorRadius,
                          d.segmentsA, d.segmentsB);
            break;
        case PrimitiveKind::Plane:
            std::snprintf(buf, sizeof(buf), "  primitive plane %.6f\n", d.halfExtent);
            break;
        case PrimitiveKind::None:
            return;   // nothing to write
    }
    f << buf;
}

// --- Read helpers ---------------------------------------------------------------

Vec3 ParseVec3(std::istringstream& ss) {
    Vec3 v;
    ss >> v.x >> v.y >> v.z;
    return v;
}

} // namespace

bool SaveScene(const char* path, const Scene& scene, const Camera& camera) {
    std::error_code ec;   // create_directories: report via return value, never throw
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "Failed to save scene: %s\n", path);
        return false;
    }

    f << "# ToonEngine scene\n\n";

    WriteVec3(f, "camera.pivot", camera.pivot);
    WriteFloat(f, "camera.distance", camera.distance);
    WriteFloat(f, "camera.yaw", camera.yaw);
    WriteFloat(f, "camera.pitch", camera.pitch);
    WriteFloat(f, "camera.fov", camera.fovY);
    f << "\n";

    // Entities are written in vector order — parents always precede children (the scene's
    // own invariant, see scene.h) — so "parent <index>" on load names an already-parsed
    // entity and LoadScene's append-in-file-order rebuild reproduces the exact same vector.
    for (const Entity& e : scene.entities) {
        f << "entity \"" << e.name << "\"\n";
        f << "  parent " << e.parent << "\n";

        if (e.transform) {   // absent only for a pure anchor node (the root)
            WriteVec3(f, "  position", e.transform->position);
            WriteVec3(f, "  rotation", e.transform->rotationEuler);
            WriteVec3(f, "  scale", e.transform->scale);
        }

        if (e.primitive.kind != PrimitiveKind::None) {
            WritePrimitive(f, e.primitive);
        } else if (!e.modelPath.empty()) {
            f << "  model " << e.modelPath << "\n";
        }

        if (e.mesh != MeshHandle::Invalid || e.model != ModelHandle::Invalid) {
            const Material& m = e.material;
            WriteVec3(f, "  material.baseColor", m.baseColor);
            WriteVec3(f, "  material.outlineColor", m.outlineColor);
            WriteFloat(f, "  material.outlineWidth", m.outlineWidth);
            WriteFloat(f, "  material.bands", m.bands);
            WriteFloat(f, "  material.ambient", m.ambient);
            WriteFloat(f, "  material.roughness", m.roughness);
        }

        if (e.light) {
            WriteVec3(f, "  light.color", e.light->color);
            WriteFloat(f, "  light.intensity", e.light->intensity);
        }

        // One line per script: "script <Name> <field...>" — the name resolves through
        // the registry on load (see below); the fields are whatever that script's own
        // Save writes, exactly like "primitive <kind> <field...>" above.
        for (const ScriptComponent& sc : e.scripts) {
            if (!sc.instance) continue;
            f << "  script " << sc.name;
            sc.instance->Save(f);
            f << "\n";
        }
        f << "\n";
    }

    std::printf("Scene saved: %s (%zu entities)\n", path, scene.entities.size());
    return true;
}

bool LoadScene(const char* path, Scene& scene, Camera& camera, Renderer& renderer) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "Failed to load scene: %s\n", path);
        return false;
    }

    // Parse into a side buffer and only replace `scene`/`camera` once the whole file is
    // read — a malformed file then leaves the caller's current scene untouched, matching
    // this function's documented failure contract.
    Scene  loaded;
    Camera loadedCamera = camera;
    Entity* cur = nullptr;
    std::string line;

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string key;
        ss >> key;

        if (key == "camera.pivot")    { loadedCamera.pivot = ParseVec3(ss); continue; }
        if (key == "camera.distance") { ss >> loadedCamera.distance; continue; }
        if (key == "camera.yaw")      { ss >> loadedCamera.yaw; continue; }
        if (key == "camera.pitch")    { ss >> loadedCamera.pitch; continue; }
        if (key == "camera.fov")      { ss >> loadedCamera.fovY; continue; }

        if (key == "entity") {
            loaded.entities.emplace_back();
            cur = &loaded.entities.back();
            cur->transform.reset();   // set by position/rotation/scale below; absent -> anchor node
            cur->parent = -1;         // overwritten by the "parent" line that always follows
            const size_t q1 = line.find('"');
            const size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) { cur->name = line.substr(q1 + 1, q2 - q1 - 1); }
            continue;
        }
        if (!cur) continue;   // a property line before the first "entity" header — ignore

        if (key == "parent") {
            ss >> cur->parent;
        } else if (key == "position") {
            if (!cur->transform) cur->transform.emplace();
            cur->transform->position = ParseVec3(ss);
        } else if (key == "rotation") {
            if (!cur->transform) cur->transform.emplace();
            cur->transform->rotationEuler = ParseVec3(ss);
        } else if (key == "scale") {
            if (!cur->transform) cur->transform.emplace();
            cur->transform->scale = ParseVec3(ss);
        } else if (key == "primitive") {
            std::string kind;
            ss >> kind;
            PrimitiveDesc& d = cur->primitive;
            if (kind == "sphere")     { d.kind = PrimitiveKind::Sphere; ss >> d.radius >> d.segmentsA >> d.segmentsB; }
            else if (kind == "cube")  { d.kind = PrimitiveKind::Cube; ss >> d.halfExtent; }
            else if (kind == "torus") { d.kind = PrimitiveKind::Torus; ss >> d.radius >> d.minorRadius >> d.segmentsA >> d.segmentsB; }
            else if (kind == "plane") { d.kind = PrimitiveKind::Plane; ss >> d.halfExtent; }
            if (d.kind != PrimitiveKind::None) {
                const MeshData mesh = MakePrimitiveMesh(d);
                cur->mesh = renderer.CreateMesh(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size()),
                                                mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size()));
            }
        } else if (key == "model") {
            ss >> cur->modelPath;   // a single token — fine for the baked TOON_MODELS_DIR paths this writes
            cur->model = renderer.LoadModel(cur->modelPath.c_str());
        } else if (key == "material.baseColor") {
            cur->material.baseColor = ParseVec3(ss);
        } else if (key == "material.outlineColor") {
            cur->material.outlineColor = ParseVec3(ss);
        } else if (key == "material.outlineWidth") {
            ss >> cur->material.outlineWidth;
        } else if (key == "material.bands") {
            ss >> cur->material.bands;
        } else if (key == "material.ambient") {
            ss >> cur->material.ambient;
        } else if (key == "material.roughness") {
            ss >> cur->material.roughness;
        } else if (key == "light.color") {
            if (!cur->light) cur->light.emplace();
            cur->light->color = ParseVec3(ss);
        } else if (key == "light.intensity") {
            if (!cur->light) cur->light.emplace();
            ss >> cur->light->intensity;
        } else if (key == "script") {
            std::string scriptName;
            ss >> scriptName;
            // CreateScript resolves the subclass by name; Load then reads the rest of
            // THIS line as that script's own fields (see script.h). A name this build
            // doesn't recognize logs and is skipped, not fatal to the rest of the load.
            if (auto instance = CreateScript(scriptName)) {
                instance->Load(ss);
                cur->scripts.push_back({scriptName, std::move(instance)});
            }
        }
    }

    EnsureSceneRoot(loaded);   // defensive: a hand-edited file might omit the root entity
    loaded.selected = -1;

    scene  = std::move(loaded);
    camera = loadedCamera;

    std::printf("Scene loaded: %s (%zu entities)\n", path, scene.entities.size());
    return true;
}

} // namespace toon
