// Model loading implementation.
//
// glTF: parsed by cgltf — iterates meshes/primitives, reads position/normal/
//   texcoord accessors, extracts PBR base color + texture from materials.
// FBX:  parsed by ufbx — iterates meshes, triangulates faces, extracts
//   diffuse color + texture from materials.
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

static void ExpandBounds(glm::vec3& bmin, glm::vec3& bmax, float x, float y, float z) {
    bmin.x = std::min(bmin.x, x);
    bmin.y = std::min(bmin.y, y);
    bmin.z = std::min(bmin.z, z);
    bmax.x = std::max(bmax.x, x);
    bmax.y = std::max(bmax.y, y);
    bmax.z = std::max(bmax.z, z);
}

// Generate smooth normals by accumulating face normals per-vertex.
// Vertex data is interleaved: [pos(3) norm(3) uv(2)] × vertCount.
// Normals at offset 3,4,5 in each 8-float vertex must be zeroed before calling.
static void GenerateNormals(float* verts, size_t vertCount,
                            const std::uint32_t* indices, size_t indexCount) {
    auto addFaceNormal = [&](size_t i0, size_t i1, size_t i2) {
        float* v0 = &verts[i0 * 8];
        float* v1 = &verts[i1 * 8];
        float* v2 = &verts[i2 * 8];
        glm::vec3 p0(v0[0], v0[1], v0[2]);
        glm::vec3 p1(v1[0], v1[1], v1[2]);
        glm::vec3 p2(v2[0], v2[1], v2[2]);
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        // Accumulate (not normalized yet — larger faces contribute more).
        for (int a = 0; a < 3; ++a) { v0[3+a] += n[a]; v1[3+a] += n[a]; v2[3+a] += n[a]; }
    };

    if (indices && indexCount >= 3) {
        for (size_t i = 0; i + 2 < indexCount; i += 3)
            addFaceNormal(indices[i], indices[i+1], indices[i+2]);
    } else {
        for (size_t i = 0; i + 2 < vertCount; i += 3)
            addFaceNormal(i, i+1, i+2);
    }

    for (size_t i = 0; i < vertCount; ++i) {
        float* n = &verts[i * 8 + 3];
        glm::vec3 nv(n[0], n[1], n[2]);
        float len = glm::length(nv);
        if (len > 1e-6f) { nv /= len; }
        else { nv = glm::vec3(0.0f, 1.0f, 0.0f); }
        n[0] = nv.x; n[1] = nv.y; n[2] = nv.z;
    }
}

// Try to load a texture from a file path relative to the model directory.
static Texture TryLoadTexture(const std::filesystem::path& modelDir,
                               const char* relPath) {
    Texture tex{};
    if (!relPath || relPath[0] == '\0') return tex;
    auto resolved = (modelDir / relPath).string();
    if (LoadTexture(tex, resolved.c_str())) {
        std::printf("  Loaded texture: %s\n", resolved.c_str());
    }
    return tex;
}

// Load a texture from a cgltf image — handles both file URIs and
// embedded buffer views (base64 data URIs in .gltf, binary chunks in .glb).
static Texture LoadGltfImage(const cgltf_image* image,
                              const std::filesystem::path& modelDir) {
    Texture tex{};
    if (!image) return tex;

    // Embedded image: data sits in a buffer view.
    if (image->buffer_view && image->buffer_view->buffer) {
        const auto* bv = image->buffer_view;
        const auto* bytes = static_cast<const unsigned char*>(bv->buffer->data);
        if (bytes) {
            auto dataPtr = bytes + bv->offset;
            auto dataLen = static_cast<int>(bv->size);
            if (LoadTextureFromMemory(tex, dataPtr, dataLen)) {
                std::printf("  Loaded embedded texture: %s\n",
                            image->name ? image->name : "(unnamed)");
            }
        }
        return tex;
    }

    // External file URI (skip data: URIs — those should have buffer_view).
    if (image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
        tex = TryLoadTexture(modelDir, image->uri);
    }
    return tex;
}

// Extract Material from a cgltf primitive's material (PBR base color).
static Material ExtractGltfMaterial(const cgltf_primitive& prim,
                                     const std::filesystem::path& modelDir) {
    Material mat;
    if (!prim.material) return mat;

    if (prim.material->has_pbr_metallic_roughness) {
        const float* bc = prim.material->pbr_metallic_roughness.base_color_factor;
        mat.baseColor = glm::vec4(bc[0], bc[1], bc[2], bc[3]);

        const cgltf_texture_view& tv =
            prim.material->pbr_metallic_roughness.base_color_texture;
        if (tv.texture && tv.texture->image) {
            mat.texture = LoadGltfImage(tv.texture->image, modelDir);
        }
    }
    return mat;
}

// Extract Material from a specific ufbx material.
static Material ExtractFbxMaterial(const ufbx_material* fbxMat,
                                    const std::filesystem::path& modelDir) {
    Material mat;
    if (!fbxMat) return mat;

    // Base color from PBR or legacy diffuse.
    ufbx_material_map colorMap = fbxMat->pbr.base_color;
    if (!colorMap.has_value)
        colorMap = fbxMat->fbx.diffuse_color;

    if (colorMap.has_value) {
        mat.baseColor = glm::vec4(
            static_cast<float>(colorMap.value_vec4.x),
            static_cast<float>(colorMap.value_vec4.y),
            static_cast<float>(colorMap.value_vec4.z),
            static_cast<float>(colorMap.value_vec4.w));
    }

    // Try to load diffuse/base color texture.
    // Attempt relative path first, then absolute, then just the filename
    // (FBX files often store stale absolute paths from the artist's machine).
    ufbx_texture* tex = colorMap.texture;
    if (tex) {
        if (tex->relative_filename.length > 0)
            mat.texture = TryLoadTexture(modelDir, tex->relative_filename.data);

        if (!mat.texture.id && tex->filename.length > 0)
            mat.texture = TryLoadTexture(modelDir, tex->filename.data);

        if (!mat.texture.id && tex->filename.length > 0) {
            auto basename = std::filesystem::path(tex->filename.data).filename().string();
            mat.texture = TryLoadTexture(modelDir, basename.c_str());
        }
    }

    return mat;
}

// ---------------------------------------------------------------------------
// glTF loader (cgltf).
// ---------------------------------------------------------------------------
static LoadedModel LoadGltf(const char* path) {
    LoadedModel result;
    glm::vec3 bmin{kFloatMax};
    glm::vec3 bmax{-kFloatMax};
    auto modelDir = std::filesystem::path(path).parent_path();

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

            auto vertCount = static_cast<size_t>(posAcc->count);
            std::vector<float> verts(vertCount * 8, 0.0f);

            for (size_t vi = 0; vi < vertCount; ++vi) {
                float pos[3] = {0.0f, 0.0f, 0.0f};
                float uv[2]  = {0.0f, 0.0f};

                cgltf_accessor_read_float(posAcc, vi, pos, 3);
                if (uvAcc) cgltf_accessor_read_float(uvAcc, vi, uv, 2);

                ExpandBounds(bmin, bmax, pos[0], pos[1], pos[2]);

                float* v = &verts[vi * 8];
                v[0] = pos[0]; v[1] = pos[1]; v[2] = pos[2];
                // normals filled below (from accessor or generated)
                v[6] = uv[0]; v[7] = uv[1];
            }

            // Read normals from accessor if available.
            if (normAcc) {
                for (size_t vi = 0; vi < vertCount; ++vi) {
                    float norm[3] = {0.0f, 0.0f, 1.0f};
                    cgltf_accessor_read_float(normAcc, vi, norm, 3);
                    float* v = &verts[vi * 8];
                    v[3] = norm[0]; v[4] = norm[1]; v[5] = norm[2];
                }
            }

            std::vector<std::uint32_t> indices;
            if (prim.indices) {
                indices.resize(static_cast<size_t>(prim.indices->count));
                for (size_t ii = 0; ii < indices.size(); ++ii) {
                    indices[ii] = static_cast<std::uint32_t>(
                        cgltf_accessor_read_index(prim.indices, ii));
                }
            }

            // Generate smooth normals if the mesh has none.
            if (!normAcc) {
                std::printf("  Generating normals for primitive %zu/%zu\n", pi, mi);
                GenerateNormals(verts.data(), vertCount,
                                indices.empty() ? nullptr : indices.data(),
                                indices.size());
            }

            SubMesh sm;
            sm.mesh = CreateMesh(
                verts.data(), verts.size() * sizeof(float), kModelStride,
                kModelAttribs, kModelAttribCount,
                static_cast<GLsizei>(vertCount),
                indices.empty() ? nullptr : indices.data(),
                static_cast<GLsizei>(indices.size()));
            sm.material = ExtractGltfMaterial(prim, modelDir);
            result.subMeshes.push_back(std::move(sm));
        }
    }

    cgltf_free(data);

    if (!result.subMeshes.empty()) {
        result.boundsMin = bmin;
        result.boundsMax = bmax;
        std::printf("Loaded %zu mesh(es) from %s (glTF)\n",
                    result.subMeshes.size(), path);
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
    auto modelDir = std::filesystem::path(path).parent_path();

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

        size_t maxTriVerts = mesh->max_face_triangles * 3;
        std::vector<uint32_t> triIndices(maxTriVerts);

        // Helper: triangulate a face and append vertices to a buffer.
        auto triangulateFace = [&](ufbx_face face, std::vector<float>& verts) {
            size_t numTris = ufbx_triangulate_face(
                triIndices.data(), triIndices.size(), mesh, face);

            for (size_t ti = 0; ti < numTris * 3; ++ti) {
                uint32_t idx = triIndices[ti];

                ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, idx);
                ufbx_vec3 norm{}; norm.x = 0.0; norm.y = 0.0; norm.z = 1.0;
                if (mesh->vertex_normal.exists)
                    norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, idx);
                ufbx_vec2 uv{};
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
        };

        // Split mesh by material — one SubMesh per material partition.
        // If no material_parts exist, fall back to one SubMesh for all faces.
        size_t numParts = mesh->material_parts.count;

        if (numParts == 0) {
            // No material partitioning — single SubMesh for entire mesh.
            std::vector<float> verts;
            for (size_t fi = 0; fi < mesh->faces.count; ++fi)
                triangulateFace(mesh->faces.data[fi], verts);

            auto vertCount = static_cast<GLsizei>(verts.size() / 8);
            if (vertCount == 0) continue;

            SubMesh sm;
            sm.mesh = CreateMesh(
                verts.data(), verts.size() * sizeof(float), kModelStride,
                kModelAttribs, kModelAttribCount, vertCount);
            const ufbx_material* mat = (mesh->materials.count > 0)
                ? mesh->materials.data[0] : nullptr;
            sm.material = ExtractFbxMaterial(mat, modelDir);
            result.subMeshes.push_back(std::move(sm));
        } else {
            for (size_t pi = 0; pi < numParts; ++pi) {
                const ufbx_mesh_part& part = mesh->material_parts.data[pi];
                if (part.num_faces == 0) continue;

                std::vector<float> verts;
                for (size_t fi = 0; fi < part.face_indices.count; ++fi) {
                    uint32_t faceIdx = part.face_indices.data[fi];
                    triangulateFace(mesh->faces.data[faceIdx], verts);
                }

                auto vertCount = static_cast<GLsizei>(verts.size() / 8);
                if (vertCount == 0) continue;

                SubMesh sm;
                sm.mesh = CreateMesh(
                    verts.data(), verts.size() * sizeof(float), kModelStride,
                    kModelAttribs, kModelAttribCount, vertCount);
                const ufbx_material* mat = (part.index < mesh->materials.count)
                    ? mesh->materials.data[part.index] : nullptr;
                sm.material = ExtractFbxMaterial(mat, modelDir);
                result.subMeshes.push_back(std::move(sm));
            }
        }
    }

    ufbx_free_scene(scene);

    if (!result.subMeshes.empty()) {
        result.boundsMin = bmin;
        result.boundsMax = bmax;
        std::printf("Loaded %zu mesh(es) from %s (FBX)\n",
                    result.subMeshes.size(), path);
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
