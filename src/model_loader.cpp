// Model loading implementation.
//
// glTF: parsed by cgltf — iterates meshes/primitives, reads position/normal/
//   texcoord accessors, builds interleaved vertex data + index buffer.
// FBX:  parsed by ufbx — iterates meshes, triangulates faces, reads vertex
//   attributes per face-corner.
//
// Both loaders track the axis-aligned bounding box of all positions so the
// caller can auto-fit the model to a reasonable scale.

#include "model_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

// Suppress warnings from third-party single-header implementations.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "ufbx.h"

// Vertex layout shared by all loaded models:
//   location 0: position  (vec3)  offset 0
//   location 1: normal    (vec3)  offset 12
//   location 2: texcoord  (vec2)  offset 24
//   stride: 32 bytes (8 floats)
static constexpr int kModelStride = 8 * static_cast<int>(sizeof(float));

static const VertexAttrib kModelAttribs[] = {
    {3, GL_FLOAT, 0},
    {3, GL_FLOAT, 3 * sizeof(float)},
    {2, GL_FLOAT, 6 * sizeof(float)},
};
static constexpr int kModelAttribCount = 3;

static constexpr float kFloatMax = std::numeric_limits<float>::max();

// Expand bounds to include a position.
static void ExpandBounds(glm::vec3& bmin, glm::vec3& bmax, float x, float y, float z) {
    bmin.x = std::min(bmin.x, x);
    bmin.y = std::min(bmin.y, y);
    bmin.z = std::min(bmin.z, z);
    bmax.x = std::max(bmax.x, x);
    bmax.y = std::max(bmax.y, y);
    bmax.z = std::max(bmax.z, z);
}

// ---------------------------------------------------------------------------
// glTF loader (cgltf).
// ---------------------------------------------------------------------------
static LoadedModel LoadGltf(const char* path) {
    LoadedModel result;
    glm::vec3 bmin{kFloatMax};
    glm::vec3 bmax{-kFloatMax};

    cgltf_options options{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        std::fprintf(stderr, "cgltf: failed to parse %s\n", path);
        return result;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        std::fprintf(stderr, "cgltf: failed to load buffers for %s\n", path);
        cgltf_free(data);
        return result;
    }

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        cgltf_mesh& mesh = data->meshes[mi];

        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            // Find the attribute accessors we care about.
            cgltf_accessor* posAcc  = nullptr;
            cgltf_accessor* normAcc = nullptr;
            cgltf_accessor* uvAcc   = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                switch (prim.attributes[ai].type) {
                case cgltf_attribute_type_position: posAcc  = prim.attributes[ai].data; break;
                case cgltf_attribute_type_normal:   normAcc = prim.attributes[ai].data; break;
                case cgltf_attribute_type_texcoord: uvAcc   = prim.attributes[ai].data; break;
                default: break;
                }
            }
            if (!posAcc) continue;

            // Build interleaved vertex data.
            auto vertCount = static_cast<size_t>(posAcc->count);
            std::vector<float> verts(vertCount * 8);

            for (size_t vi = 0; vi < vertCount; ++vi) {
                float pos[3]  = {0.0f, 0.0f, 0.0f};
                float norm[3] = {0.0f, 0.0f, 1.0f};
                float uv[2]   = {0.0f, 0.0f};

                cgltf_accessor_read_float(posAcc, vi, pos, 3);
                if (normAcc) cgltf_accessor_read_float(normAcc, vi, norm, 3);
                if (uvAcc)   cgltf_accessor_read_float(uvAcc,   vi, uv,   2);

                ExpandBounds(bmin, bmax, pos[0], pos[1], pos[2]);

                float* v = &verts[vi * 8];
                v[0] = pos[0];  v[1] = pos[1];  v[2] = pos[2];
                v[3] = norm[0]; v[4] = norm[1]; v[5] = norm[2];
                v[6] = uv[0];  v[7] = uv[1];
            }

            // Read index buffer (if present).
            std::vector<std::uint32_t> indices;
            if (prim.indices) {
                indices.resize(static_cast<size_t>(prim.indices->count));
                for (size_t ii = 0; ii < indices.size(); ++ii) {
                    indices[ii] = static_cast<std::uint32_t>(
                        cgltf_accessor_read_index(prim.indices, ii));
                }
            }

            result.meshes.push_back(CreateMesh(
                verts.data(), verts.size() * sizeof(float), kModelStride,
                kModelAttribs, kModelAttribCount,
                static_cast<GLsizei>(vertCount),
                indices.empty() ? nullptr : indices.data(),
                static_cast<GLsizei>(indices.size())));
        }
    }

    cgltf_free(data);

    if (!result.meshes.empty()) {
        result.boundsMin = bmin;
        result.boundsMax = bmax;
        std::printf("Loaded %zu mesh(es) from %s (glTF)\n", result.meshes.size(), path);
    }
    return result;
}

// ---------------------------------------------------------------------------
// FBX loader (ufbx).
// ---------------------------------------------------------------------------
static LoadedModel LoadFbx(const char* path) {
    LoadedModel result;
    glm::vec3 bmin{kFloatMax};
    glm::vec3 bmax{-kFloatMax};

    ufbx_load_opts opts{};
    opts.generate_missing_normals = true;

    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path, &opts, &error);
    if (!scene) {
        std::fprintf(stderr, "ufbx: failed to load %s: %s\n",
                     path, error.description.data);
        return result;
    }

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        ufbx_mesh* mesh = scene->meshes.data[mi];

        // ufbx faces may be quads/ngons — triangulate each face.
        size_t maxTriVerts = mesh->max_face_triangles * 3;
        std::vector<uint32_t> triIndices(maxTriVerts);
        std::vector<float> verts;

        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            ufbx_face face = mesh->faces.data[fi];
            size_t numTris = ufbx_triangulate_face(
                triIndices.data(), triIndices.size(), mesh, face);

            for (size_t ti = 0; ti < numTris * 3; ++ti) {
                uint32_t idx = triIndices[ti];

                ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, idx);
                ufbx_vec3 norm = {0.0, 0.0, 1.0};
                if (mesh->vertex_normal.exists)
                    norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, idx);
                ufbx_vec2 uv = {0.0, 0.0};
                if (mesh->vertex_uv.exists)
                    uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, idx);

                float px = static_cast<float>(pos.x);
                float py = static_cast<float>(pos.y);
                float pz = static_cast<float>(pos.z);
                ExpandBounds(bmin, bmax, px, py, pz);

                verts.push_back(px);
                verts.push_back(py);
                verts.push_back(pz);
                verts.push_back(static_cast<float>(norm.x));
                verts.push_back(static_cast<float>(norm.y));
                verts.push_back(static_cast<float>(norm.z));
                verts.push_back(static_cast<float>(uv.x));
                verts.push_back(static_cast<float>(uv.y));
            }
        }

        auto vertCount = static_cast<GLsizei>(verts.size() / 8);
        if (vertCount == 0) continue;

        // Non-indexed draw — vertices are fully expanded from face corners.
        result.meshes.push_back(CreateMesh(
            verts.data(), verts.size() * sizeof(float), kModelStride,
            kModelAttribs, kModelAttribCount, vertCount));
    }

    ufbx_free_scene(scene);

    if (!result.meshes.empty()) {
        result.boundsMin = bmin;
        result.boundsMax = bmax;
        std::printf("Loaded %zu mesh(es) from %s (FBX)\n", result.meshes.size(), path);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Public API — dispatch by extension.
// ---------------------------------------------------------------------------
LoadedModel LoadModel(const char* path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".gltf" || ext == ".glb") return LoadGltf(path);
    if (ext == ".fbx")                   return LoadFbx(path);

    std::fprintf(stderr, "Unsupported model format: %s (use .gltf, .glb, or .fbx)\n", ext.c_str());
    return {};
}
