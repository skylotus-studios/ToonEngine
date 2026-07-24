//============================================================================
//  core/scene/serializer.cpp: scene save/load implementation.
//
//  A simple line-based text format: "key value..." per line, two-space-indented entity
//  properties following an "entity "Name"" header, a blank line between entities. Ported
//  from ToonEngineOld/src/scene/serializer.cpp, generalized where this engine's Entity
//  differs (procedural primitives alongside loaded models; camera is orbit-style).
//============================================================================
#include "core/scene/serializer.h"

#include "core/platform/paths.h" // Assets::Sprite (exe-relative sprite texture paths)
#include "core/rendering/primitives.h"
#include "core/scene/script.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace toon {

    namespace {

        // --- Write helpers ------------------------------------------------------------

        void WriteVec3(std::ofstream &f, const char *key, const Vec3 &v) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s %.6f %.6f %.6f\n", key, v.x, v.y, v.z);
            f << buf;
        }

        void WriteFloat(std::ofstream &f, const char *key, float v) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s %.6f\n", key, v);
            f << buf;
        }

        void WriteQuat(std::ofstream &f, const char *key, const Quat &q) {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "%s %.6f %.6f %.6f %.6f\n", key, q.x, q.y, q.z, q.w);
            f << buf;
        }

        // "collider <box|sphere|capsule> <ex ey ez>". `extents`' meaning depends on shape (see
        // BodyDesc's own comment: Box = half-extents, Sphere = radius in .x, Capsule = half-height
        // in .x + radius in .y) but is always 3 floats, so (unlike WritePrimitive below) one
        // line shape covers every ColliderShape; load only needs to parse the shape name once.
        void WriteCollider(std::ofstream &f, const ColliderComponent &c) {
            const char *kind = c.shape == ColliderShape::Sphere    ? "sphere"
                               : c.shape == ColliderShape::Capsule ? "capsule"
                                                                   : "box";
            char buf[192];
            std::snprintf(buf, sizeof(buf), "  collider %s %.6f %.6f %.6f\n", kind, c.extents.x, c.extents.y,
                          c.extents.z);
            f << buf;
        }

        // "rigidbody <static|dynamic|kinematic> <mass friction restitution>". The runtime
        // BodyHandle is deliberately NOT written: like Entity::worldMatrix, it's populated when
        // Play builds the physics world (main.cpp), not part of the saved scene's data.
        void WriteRigidBody(std::ofstream &f, const RigidBodyComponent &b) {
            const char *kind = b.type == BodyType::Static      ? "static"
                               : b.type == BodyType::Kinematic ? "kinematic"
                                                               : "dynamic";
            char buf[192];
            std::snprintf(buf, sizeof(buf), "  rigidbody %s %.6f %.6f %.6f\n", kind, b.mass, b.friction, b.restitution);
            f << buf;
        }

        // "audio <clip> <volume> <pitch> <loop> <autoplay> <spatial> <stream> <maxDistance>".
        // `handle` is deliberately NOT written: like RigidBodyComponent::handle, it's populated
        // when Play builds the audio world (app/audio_glue.cpp), not part of the saved scene's
        // data. `clip` is written as a single whitespace-free token, same convention as `model`.
        void WriteAudioSource(std::ofstream &f, const AudioSource &a) {
            char buf[320];
            std::snprintf(buf, sizeof(buf), "  audio %s %.6f %.6f %d %d %d %d %.6f\n",
                          a.clip.empty() ? "-" : a.clip.c_str(), a.volume, a.pitch, a.loop ? 1 : 0, a.autoplay ? 1 : 0,
                          a.spatial ? 1 : 0, a.stream ? 1 : 0, a.maxDistance);
            f << buf;
        }

        // "animation <clipIndex> <playing> <looping>". `time`/`prevTime` are deliberately NOT
        // written: unlike `transform` (an authored pose), they're a transient playback cursor
        // that only means anything mid-session -- same "runtime state, not scene data"
        // treatment as RigidBodyComponent::handle/AudioSource::handle above, just without an
        // actual handle to skip. A loaded entity always starts its clip fresh, from time 0.
        void WriteAnimation(std::ofstream &f, const AnimationComponent &a) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "  animation %d %d %d\n", a.clipIndex, a.playing ? 1 : 0, a.looping ? 1 : 0);
            f << buf;
        }

        // "sprite <texturePath|-> <tint xyzw> <uvRect xyzw> <flipX> <flipY>". `texture` (the
        // runtime handle) is deliberately NOT written: like AudioSource::handle, it's rebuilt
        // from `texturePath` on load, not part of the saved scene's data. `texturePath` is
        // written as a single whitespace-free token, same convention as `model`/audio's clip.
        void WriteSpriteComponent(std::ofstream &f, const SpriteComponent &s) {
            char buf[384];
            std::snprintf(buf, sizeof(buf), "  sprite %s %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d %d\n",
                          s.texturePath.empty() ? "-" : s.texturePath.c_str(), s.tint.x, s.tint.y, s.tint.z, s.tint.w,
                          s.uvRect.x, s.uvRect.y, s.uvRect.z, s.uvRect.w, s.flipX ? 1 : 0, s.flipY ? 1 : 0);
            f << buf;
        }

        // One line per PrimitiveKind, since each generator takes a different param list (see
        // PrimitiveDesc's field comments in primitives.h for the kind -> field mapping).
        void WritePrimitive(std::ofstream &f, const PrimitiveDesc &d) {
            char buf[192];
            switch (d.kind) {
                case PrimitiveKind::Sphere:
                    std::snprintf(buf, sizeof(buf), "  primitive sphere %.6f %u %u\n", d.radius, d.segmentsA,
                                  d.segmentsB);
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
                    return; // nothing to write
            }
            f << buf;
        }

        // --- Read helpers ---------------------------------------------------------------

        Vec3 ParseVec3(std::istringstream &ss) {
            Vec3 v;
            ss >> v.x >> v.y >> v.z;
            return v;
        }

    } // namespace

    bool SaveScene(const char *path, const Scene &scene, const Camera &camera) {
        std::error_code ec; // create_directories: report via return value, never throw
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

        // Entities are written in vector order: parents always precede children (the scene's
        // own invariant, see scene.h), so "parent <index>" on load names an already-parsed
        // entity and LoadScene's append-in-file-order rebuild reproduces the exact same vector.
        for (const Entity &e : scene.entities) {
            f << "entity \"" << e.name << "\"\n";
            f << "  parent " << e.parent << "\n";

            if (e.transform) { // absent only for a pure anchor node (the root)
                WriteVec3(f, "  position", e.transform->position);
                WriteQuat(f, "  rotation", e.transform->rotation);
                WriteVec3(f, "  scale", e.transform->scale);
            }

            if (e.primitive.kind != PrimitiveKind::None) {
                WritePrimitive(f, e.primitive);
            } else if (!e.modelPath.empty()) {
                f << "  model " << e.modelPath << "\n";
            }

            if (e.mesh != MeshHandle::Invalid || e.model != ModelHandle::Invalid) {
                const Material &m = e.material;
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

            if (e.camera) {
                const CameraComponent &c = *e.camera;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "  camera %.6f %.6f %.6f %d %.6f %d\n", c.fovY, c.nearZ, c.farZ,
                              c.orthographic ? 1 : 0, c.orthoHeight, c.primary ? 1 : 0);
                f << buf;
            }

            if (e.collider) { WriteCollider(f, *e.collider); }
            if (e.body) { WriteRigidBody(f, *e.body); }
            if (e.audioSource) { WriteAudioSource(f, *e.audioSource); }
            if (e.animation) { WriteAnimation(f, *e.animation); }
            if (e.sprite) { WriteSpriteComponent(f, *e.sprite); }

            // One line per script: "script <Name> <field...>"; the name resolves through
            // the registry on load (see below); the fields are whatever that script's own
            // Save writes, exactly like "primitive <kind> <field...>" above.
            for (const ScriptComponent &sc : e.scripts) {
                if (!sc.instance) { continue; }
                f << "  script " << sc.name;
                sc.instance->Save(f);
                f << "\n";
            }
            f << "\n";
        }

        std::printf("Scene saved: %s (%zu entities)\n", path, scene.entities.size());
        return true;
    }

    bool LoadScene(const char *path, Scene &scene, Camera &camera, Renderer &renderer) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::fprintf(stderr, "Failed to load scene: %s\n", path);
            return false;
        }

        // Parse into a side buffer and only replace `scene`/`camera` once the whole file is
        // read. A malformed file then leaves the caller's current scene untouched, matching
        // this function's documented failure contract.
        Scene loaded;
        Camera loadedCamera = camera;
        Entity *cur = nullptr;
        std::string line;

        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') { continue; }

            std::istringstream ss(line);
            std::string key;
            ss >> key;

            if (key == "camera.pivot") {
                loadedCamera.pivot = ParseVec3(ss);
                continue;
            }
            if (key == "camera.distance") {
                ss >> loadedCamera.distance;
                continue;
            }
            if (key == "camera.yaw") {
                ss >> loadedCamera.yaw;
                continue;
            }
            if (key == "camera.pitch") {
                ss >> loadedCamera.pitch;
                continue;
            }
            if (key == "camera.fov") {
                ss >> loadedCamera.fovY;
                continue;
            }

            if (key == "entity") {
                loaded.entities.emplace_back();
                cur = &loaded.entities.back();
                cur->transform.reset(); // set by position/rotation/scale below; absent -> anchor node
                cur->parent = -1;       // overwritten by the "parent" line that always follows
                const size_t q1 = line.find('"');
                const size_t q2 = line.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    cur->name = line.substr(q1 + 1, q2 - q1 - 1);
                }
                continue;
            }
            if (!cur) {
                continue; // a property line before the first "entity" header: ignore
            }

            if (key == "parent") {
                ss >> cur->parent;
            } else if (key == "position") {
                if (!cur->transform) { cur->transform.emplace(); }
                cur->transform->position = ParseVec3(ss);
            } else if (key == "rotation") {
                if (!cur->transform) { cur->transform.emplace(); }
                // Current format is 4 floats (a quaternion, see WriteQuat above); a file saved
                // before the M2.1 quaternion refactor has 3 (Euler radians) -- detect which by
                // trying to read a 4th token, and convert the legacy form so old .scene files
                // keep loading unchanged.
                float v0, v1, v2, v3;
                ss >> v0 >> v1 >> v2;
                if (ss >> v3) {
                    cur->transform->rotation = {v0, v1, v2, v3};
                } else {
                    cur->transform->rotation = QuatFromEuler({v0, v1, v2});
                }
            } else if (key == "scale") {
                if (!cur->transform) { cur->transform.emplace(); }
                cur->transform->scale = ParseVec3(ss);
            } else if (key == "primitive") {
                std::string kind;
                ss >> kind;
                PrimitiveDesc &d = cur->primitive;
                if (kind == "sphere") {
                    d.kind = PrimitiveKind::Sphere;
                    ss >> d.radius >> d.segmentsA >> d.segmentsB;
                } else if (kind == "cube") {
                    d.kind = PrimitiveKind::Cube;
                    ss >> d.halfExtent;
                } else if (kind == "torus") {
                    d.kind = PrimitiveKind::Torus;
                    ss >> d.radius >> d.minorRadius >> d.segmentsA >> d.segmentsB;
                } else if (kind == "plane") {
                    d.kind = PrimitiveKind::Plane;
                    ss >> d.halfExtent;
                }
                if (d.kind != PrimitiveKind::None) {
                    const MeshData mesh = MakePrimitiveMesh(d);
                    cur->mesh = renderer.CreateMesh(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size()),
                                                    mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size()));
                }
            } else if (key == "model") {
                ss >> cur->modelPath; // a single token, fine for the space-free asset paths this writes
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
                if (!cur->light) { cur->light.emplace(); }
                cur->light->color = ParseVec3(ss);
            } else if (key == "light.intensity") {
                if (!cur->light) { cur->light.emplace(); }
                ss >> cur->light->intensity;
            } else if (key == "camera") {
                CameraComponent c;
                int ortho = 0, primary = 0;
                ss >> c.fovY >> c.nearZ >> c.farZ >> ortho >> c.orthoHeight >> primary;
                c.orthographic = ortho != 0;
                c.primary = primary != 0;
                cur->camera = c;
            } else if (key == "collider") {
                std::string kind;
                ss >> kind;
                ColliderComponent c;
                c.shape = kind == "sphere"    ? ColliderShape::Sphere
                          : kind == "capsule" ? ColliderShape::Capsule
                                              : ColliderShape::Box;
                c.extents = ParseVec3(ss);
                cur->collider = c;
            } else if (key == "rigidbody") {
                std::string kind;
                ss >> kind;
                RigidBodyComponent b;
                b.type = kind == "static"      ? BodyType::Static
                         : kind == "kinematic" ? BodyType::Kinematic
                                               : BodyType::Dynamic;
                ss >> b.mass >> b.friction >> b.restitution;
                cur->body = b;
            } else if (key == "audio") {
                AudioSource a;
                std::string clip;
                int loopInt = 0, autoplayInt = 0, spatialInt = 0, streamInt = 0;
                ss >> clip >> a.volume >> a.pitch >> loopInt >> autoplayInt >> spatialInt >> streamInt >> a.maxDistance;
                a.clip = (clip == "-") ? std::string() : clip;
                a.loop = loopInt != 0;
                a.autoplay = autoplayInt != 0;
                a.spatial = spatialInt != 0;
                a.stream = streamInt != 0;
                cur->audioSource = a;
            } else if (key == "animation") {
                AnimationComponent a;
                int playingInt = 0, loopingInt = 0;
                ss >> a.clipIndex >> playingInt >> loopingInt;
                a.playing = playingInt != 0;
                a.looping = loopingInt != 0;
                cur->animation = a; // time/prevTime stay 0 -- see WriteAnimation's own comment
            } else if (key == "sprite") {
                SpriteComponent s;
                std::string texPath;
                int flipXInt = 0, flipYInt = 0;
                ss >> texPath >> s.tint.x >> s.tint.y >> s.tint.z >> s.tint.w >> s.uvRect.x >> s.uvRect.y >>
                    s.uvRect.z >> s.uvRect.w >> flipXInt >> flipYInt;
                s.texturePath = (texPath == "-") ? std::string() : texPath;
                s.flipX = flipXInt != 0;
                s.flipY = flipYInt != 0;
                // srgb=true: a sprite composites into the linear HDR scene (see LoadTexture's
                // own comment), unlike the asset-browser thumbnails this loader otherwise
                // serves. texturePath is a filename resolved against the sprites/ asset dir (Assets::Sprite).
                if (!s.texturePath.empty()) {
                    s.texture = renderer.LoadTexture(Assets::Sprite(s.texturePath).c_str(), true);
                }
                cur->sprite = s;
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

        EnsureSceneRoot(loaded); // defensive: a hand-edited file might omit the root entity
        loaded.selected = -1;

        scene = std::move(loaded);
        camera = loadedCamera;

        std::printf("Scene loaded: %s (%zu entities)\n", path, scene.entities.size());
        return true;
    }

} // namespace toon
