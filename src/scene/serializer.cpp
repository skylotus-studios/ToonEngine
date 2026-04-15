// Scene serialization implementation.

#include "serializer.h"

#include "core/animator.h"
#include "scene/model_loader.h"
#include "ui/overlay.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------
static void WriteVec3(std::ofstream& f, const char* key, const glm::vec3& v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %.6f %.6f %.6f\n", key, v.x, v.y, v.z);
    f << buf;
}

static void WriteFloat(std::ofstream& f, const char* key, float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.6f\n", key, v);
    f << buf;
}

static glm::vec3 ParseVec3(std::istringstream& ss) {
    glm::vec3 v{0.0f};
    ss >> v.x >> v.y >> v.z;
    return v;
}

// ---------------------------------------------------------------------------
// Save.
// ---------------------------------------------------------------------------
bool SaveScene(const char* path, const Scene& scene,
               const Camera& camera, const RenderSettings& settings) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "Failed to save scene: %s\n", path);
        return false;
    }

    f << "# ToonEngine scene\n\n";

    // Camera.
    WriteVec3(f, "camera.position", camera.position);
    WriteFloat(f, "camera.yaw", camera.yaw);
    WriteFloat(f, "camera.pitch", camera.pitch);
    WriteFloat(f, "camera.fov", camera.fovY);
    f << "\n";

    // Render settings (subset).
    WriteVec3(f, "settings.skyTop", settings.skyTop);
    WriteVec3(f, "settings.skyBottom", settings.skyBottom);
    WriteVec3(f, "settings.clearColor", settings.clearColor);
    f << "\n";

    // Entities. Written in vector order so parent indices always refer to
    // an already-written entity on load.
    for (auto& e : scene.entities) {
        f << "entity \"" << e.name << "\"\n";
        f << "  parent " << e.parent << "\n";

        if (e.light) {
            const Light& L = *e.light;
            f << "  light " << (L.type == LightType::Directional ? "directional" : "point") << "\n";
            WriteVec3(f, "  light.direction", L.direction);
            WriteVec3(f, "  light.color", L.color);
            WriteFloat(f, "  light.intensity", L.intensity);
            WriteFloat(f, "  light.radius", L.radius);
        }

        if (!e.modelPath.empty())
            f << "  model " << e.modelPath << "\n";

        // Transform is an optional component (the scene root has none).
        if (e.transform) {
            WriteVec3(f, "  position", e.transform->position);
            WriteVec3(f, "  rotation", e.transform->rotation);
            WriteVec3(f, "  scale", e.transform->scale);
        }
        f << "\n";
    }

    std::printf("Scene saved: %s\n", path);
    return true;
}

// ---------------------------------------------------------------------------
// Load.
// ---------------------------------------------------------------------------
bool LoadScene(const char* path, Scene& scene,
               Camera& camera, RenderSettings& settings,
               Texture& /*defaultTexture*/) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "Failed to load scene: %s\n", path);
        return false;
    }

    DestroyScene(scene);

    Entity* cur = nullptr;
    std::string line;

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string key;
        ss >> key;

        // Camera.
        if (key == "camera.position") { camera.position = ParseVec3(ss); continue; }
        if (key == "camera.yaw")   { ss >> camera.yaw; continue; }
        if (key == "camera.pitch") { ss >> camera.pitch; continue; }
        if (key == "camera.fov")   { ss >> camera.fovY; continue; }

        // Settings.
        if (key == "settings.skyTop")    { settings.skyTop = ParseVec3(ss); continue; }
        if (key == "settings.skyBottom") { settings.skyBottom = ParseVec3(ss); continue; }
        if (key == "settings.clearColor"){ settings.clearColor = ParseVec3(ss); continue; }

        // New entity.
        if (key == "entity") {
            scene.entities.emplace_back();
            cur = &scene.entities.back();
            // Start with no transform component; position/rotation/scale
            // lines below will create one if present (root has none).
            cur->transform.reset();
            // parent defaults to -1 (orphan) so legacy files with no parent
            // lines get re-parented to the synthesized root on load.
            cur->parent = -1;
            // Parse quoted name.
            auto q1 = line.find('"');
            auto q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                cur->name = line.substr(q1 + 1, q2 - q1 - 1);
            continue;
        }

        if (!cur) continue;

        if (key == "parent") { ss >> cur->parent; continue; }

        // Entity properties.
        if (key == "light") {
            std::string type; ss >> type;
            cur->light = Light{};
            cur->light->type = (type == "point") ? LightType::Point : LightType::Directional;
        }
        else if (key == "light.direction" && cur->light) cur->light->direction = ParseVec3(ss);
        else if (key == "light.color" && cur->light)     cur->light->color = ParseVec3(ss);
        else if (key == "light.intensity" && cur->light)  ss >> cur->light->intensity;
        else if (key == "light.radius" && cur->light)     ss >> cur->light->radius;
        else if (key == "model") {
            std::string modelPath; ss >> modelPath;
            cur->modelPath = modelPath;
            cur->shading = ShadingMode::Toon;

            LoadedModel loaded = LoadModel(modelPath.c_str());
            if (!loaded.subMeshes.empty()) {
                cur->subMeshes = std::move(loaded.subMeshes);
                cur->skeleton  = std::move(loaded.skeleton);
                cur->clips     = std::move(loaded.clips);
                cur->skinned   = loaded.skinned;
                if (cur->skinned && !cur->clips.empty())
                    AnimatorSetClip(cur->animator, cur->skeleton, 0);
            }
        }
        else if (key == "position") {
            if (!cur->transform) cur->transform.emplace();
            cur->transform->position = ParseVec3(ss);
        }
        else if (key == "rotation") {
            if (!cur->transform) cur->transform.emplace();
            cur->transform->rotation = ParseVec3(ss);
        }
        else if (key == "scale") {
            if (!cur->transform) cur->transform.emplace();
            cur->transform->scale = ParseVec3(ss);
        }
    }

    // Backwards compatibility: files written before hierarchy support have
    // no "parent" line and no root entity. Synthesize a root if needed.
    EnsureSceneRoot(scene);

    std::printf("Scene loaded: %s (%zu entities)\n", path, scene.entities.size());
    return true;
}
