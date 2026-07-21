// Model loading implementation.
//
// glTF: parsed by cgltf — meshes, materials, skeleton, animations, skinning.
// FBX:  parsed by ufbx — meshes, materials, skeleton, baked animations, skinning.
//
// Both loaders track the AABB so the caller can auto-fit.

#include "model_loader.h"

#include "core/renderer.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

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

// ---------------------------------------------------------------------------
// Vertex layouts.
// ---------------------------------------------------------------------------

// Non-skinned: pos(3) + norm(3) + uv(2) + smoothNorm(3) = 11 floats, stride 44.
// Smooth normal at location 5 (skipping bone locations 3,4).
static constexpr int kStride     = 11 * static_cast<int>(sizeof(float));
static constexpr int kFloatsPerV = 11;
static constexpr int kSmoothNormOffset = 8;   // float index within vertex
static const VertexAttrib kAttribs[] = {
    {3, AttribType::Float, 0},
    {3, AttribType::Float, 3  * sizeof(float)},
    {2, AttribType::Float, 6  * sizeof(float)},
    {3, AttribType::Float, 8  * sizeof(float), /*location=*/5},  // smooth normal
};
static constexpr int kAttribCount = 4;

// Skinned: pos(3) + norm(3) + uv(2) + boneIds(4) + weights(4) + smoothNorm(3) = 19 floats, stride 76.
static constexpr int kSkinStride     = 19 * static_cast<int>(sizeof(float));
static constexpr int kSkinFloatsPerV = 19;
static constexpr int kSkinSmoothNormOffset = 16;
static const VertexAttrib kSkinAttribs[] = {
    {3, AttribType::Float, 0},
    {3, AttribType::Float, 3  * sizeof(float)},
    {2, AttribType::Float, 6  * sizeof(float)},
    {4, AttribType::Float, 8  * sizeof(float)},                   // bone IDs
    {4, AttribType::Float, 12 * sizeof(float)},                   // bone weights
    {3, AttribType::Float, 16 * sizeof(float), /*location=*/5},   // smooth normal
};
static constexpr int kSkinAttribCount = 6;

static constexpr float kFloatMax = std::numeric_limits<float>::max();

static void ExpandBounds(glm::vec3& bmin, glm::vec3& bmax, float x, float y, float z) {
    bmin.x = std::min(bmin.x, x); bmin.y = std::min(bmin.y, y); bmin.z = std::min(bmin.z, z);
    bmax.x = std::max(bmax.x, x); bmax.y = std::max(bmax.y, y); bmax.z = std::max(bmax.z, z);
}

// Compute smooth (averaged-per-position) normals for outline extrusion.
// At hard edges, adjacent faces have different shading normals — extruding
// along those creates gaps.  Smooth normals average all shading normals
// that share the same vertex position, giving a continuous extrusion.
static void ComputeSmoothNormals(float* verts, size_t vertCount, int stride,
                                  int smoothOffset) {
    // Group vertex indices by quantized position.
    struct PosKey {
        int64_t x, y, z;
        bool operator==(const PosKey& o) const { return x==o.x && y==o.y && z==o.z; }
    };
    struct PosHash {
        size_t operator()(const PosKey& k) const {
            size_t h = 0;
            h ^= std::hash<int64_t>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<PosKey, std::vector<size_t>, PosHash> groups;
    for (size_t i = 0; i < vertCount; ++i) {
        float* v = &verts[i * stride];
        PosKey key{
            static_cast<int64_t>(std::round(static_cast<double>(v[0]) * 10000.0)),
            static_cast<int64_t>(std::round(static_cast<double>(v[1]) * 10000.0)),
            static_cast<int64_t>(std::round(static_cast<double>(v[2]) * 10000.0))
        };
        groups[key].push_back(i);
    }

    for (auto& [key, indices] : groups) {
        glm::vec3 avg(0.0f);
        for (size_t idx : indices) {
            float* n = &verts[idx * stride + 3];  // shading normal at offset 3
            avg += glm::vec3(n[0], n[1], n[2]);
        }
        float len = glm::length(avg);
        if (len > 1e-6f) avg /= len;
        else avg = glm::vec3(0.0f, 1.0f, 0.0f);

        for (size_t idx : indices) {
            float* sn = &verts[idx * stride + smoothOffset];
            sn[0] = avg.x; sn[1] = avg.y; sn[2] = avg.z;
        }
    }
}

// Generate smooth normals by accumulating face normals per-vertex.
// `stride` is the number of floats per vertex. Positions at offset 0..2,
// normals written to offset 3..5 (must be zeroed before calling).
static void GenerateNormals(float* verts, size_t vertCount, int stride,
                            const std::uint32_t* indices, size_t indexCount) {
    auto addFaceNormal = [&](size_t i0, size_t i1, size_t i2) {
        float* v0 = &verts[i0 * stride];
        float* v1 = &verts[i1 * stride];
        float* v2 = &verts[i2 * stride];
        glm::vec3 p0(v0[0],v0[1],v0[2]), p1(v1[0],v1[1],v1[2]), p2(v2[0],v2[1],v2[2]);
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        for (int a = 0; a < 3; ++a) { v0[3+a] += n[a]; v1[3+a] += n[a]; v2[3+a] += n[a]; }
    };
    if (indices && indexCount >= 3) {
        for (size_t i = 0; i + 2 < indexCount; i += 3)
            addFaceNormal(indices[i], indices[i+1], indices[i+2]);
    } else {
        for (size_t i = 0; i + 2 < vertCount; i += 3) addFaceNormal(i, i+1, i+2);
    }
    for (size_t i = 0; i < vertCount; ++i) {
        float* n = &verts[i * stride + 3];
        glm::vec3 nv(n[0], n[1], n[2]);
        float len = glm::length(nv);
        if (len > 1e-6f) nv /= len; else nv = glm::vec3(0.0f, 1.0f, 0.0f);
        n[0] = nv.x; n[1] = nv.y; n[2] = nv.z;
    }
}

// ---------------------------------------------------------------------------
// Texture helpers (shared by both loaders).
// ---------------------------------------------------------------------------
static TextureHandle TryLoadTexture(const std::filesystem::path& modelDir, const char* relPath) {
    if (!relPath || relPath[0] == '\0') return {};
    auto resolved = (modelDir / relPath).string();
    TextureHandle h = LoadTexture(resolved.c_str());
    if (h) std::printf("  Loaded texture: %s\n", resolved.c_str());
    return h;
}

static TextureHandle LoadGltfImage(const cgltf_image* image, const std::filesystem::path& modelDir) {
    if (!image) return {};
    if (image->buffer_view && image->buffer_view->buffer) {
        const auto* bv = image->buffer_view;
        const auto* bytes = static_cast<const unsigned char*>(bv->buffer->data);
        if (bytes) {
            TextureHandle h = LoadTextureFromMemory(bytes + bv->offset,
                                                    static_cast<int>(bv->size), /*flipY=*/false);
            if (h) std::printf("  Loaded embedded texture: %s\n", image->name ? image->name : "(unnamed)");
            return h;
        }
        return {};
    }
    if (image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
        auto resolved = (modelDir / image->uri).string();
        TextureHandle h = LoadTexture(resolved.c_str(), /*flipY=*/false);
        if (h) std::printf("  Loaded texture: %s\n", resolved.c_str());
        return h;
    }
    return {};
}

static Material ExtractGltfMaterial(const cgltf_primitive& prim,
                                     const std::filesystem::path& modelDir) {
    Material mat;
    if (!prim.material) return mat;
    if (prim.material->has_pbr_metallic_roughness) {
        const float* bc = prim.material->pbr_metallic_roughness.base_color_factor;
        mat.baseColor = glm::vec4(bc[0], bc[1], bc[2], bc[3]);
        const cgltf_texture_view& tv = prim.material->pbr_metallic_roughness.base_color_texture;
        if (tv.texture && tv.texture->image)
            mat.texture = LoadGltfImage(tv.texture->image, modelDir);
    }
    // Normal map.
    const cgltf_texture_view& nv = prim.material->normal_texture;
    if (nv.texture && nv.texture->image)
        mat.normalMap = LoadGltfImage(nv.texture->image, modelDir);
    return mat;
}

static Material ExtractFbxMaterial(const ufbx_material* fbxMat,
                                    const std::filesystem::path& modelDir) {
    Material mat;
    if (!fbxMat) return mat;
    ufbx_material_map colorMap = fbxMat->pbr.base_color;
    if (!colorMap.has_value) colorMap = fbxMat->fbx.diffuse_color;
    if (colorMap.has_value) {
        mat.baseColor = glm::vec4(
            static_cast<float>(colorMap.value_vec4.x), static_cast<float>(colorMap.value_vec4.y),
            static_cast<float>(colorMap.value_vec4.z), static_cast<float>(colorMap.value_vec4.w));
    }
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
    // Normal map (PBR or legacy).
    ufbx_material_map normalMapProp = fbxMat->pbr.normal_map;
    if (!normalMapProp.texture)
        normalMapProp = fbxMat->fbx.normal_map;
    if (normalMapProp.texture) {
        ufbx_texture* nt = normalMapProp.texture;
        if (nt->relative_filename.length > 0)
            mat.normalMap = TryLoadTexture(modelDir, nt->relative_filename.data);
        if (!mat.normalMap.id && nt->filename.length > 0)
            mat.normalMap = TryLoadTexture(modelDir, nt->filename.data);
        if (!mat.normalMap.id && nt->filename.length > 0) {
            auto basename = std::filesystem::path(nt->filename.data).filename().string();
            mat.normalMap = TryLoadTexture(modelDir, basename.c_str());
        }
    }
    return mat;
}

// ---------------------------------------------------------------------------
// glTF loader.
// ---------------------------------------------------------------------------

// Build a node* -> joint index map for the first skin.
using NodeJointMap = std::unordered_map<const cgltf_node*, int>;

static void ExtractGltfSkeleton(cgltf_data* data, LoadedModel& result, NodeJointMap& nodeMap) {
    if (data->skins_count == 0) return;
    const cgltf_skin& skin = data->skins[0];
    int jointCount = static_cast<int>(skin.joints_count);
    if (jointCount == 0 || jointCount > kMaxJoints) return;

    result.skeleton.joints.resize(jointCount);
    result.skinned = true;

    // Build node -> index map and extract bind-pose local transforms.
    for (int i = 0; i < jointCount; ++i) {
        const cgltf_node* node = skin.joints[i];
        nodeMap[node] = i;
        Joint& j = result.skeleton.joints[i];
        j.name = node->name ? node->name : "";

        // Bind-pose local TRS from the glTF node.
        if (node->has_translation)
            j.bindPosition = glm::vec3(node->translation[0], node->translation[1], node->translation[2]);
        if (node->has_rotation)
            j.bindRotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
        if (node->has_scale)
            j.bindScale = glm::vec3(node->scale[0], node->scale[1], node->scale[2]);
    }

    // Parent indices: walk up each joint's node->parent until we find another
    // joint or hit the root.
    for (int i = 0; i < jointCount; ++i) {
        const cgltf_node* p = skin.joints[i]->parent;
        result.skeleton.joints[i].parent = -1;
        while (p) {
            auto it = nodeMap.find(p);
            if (it != nodeMap.end()) { result.skeleton.joints[i].parent = it->second; break; }
            p = p->parent;
        }
    }

    // Inverse bind matrices.
    if (skin.inverse_bind_matrices) {
        for (int i = 0; i < jointCount; ++i) {
            float m[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, i, m, 16);
            // glTF stores column-major matrices; glm::make_mat4 reads column-major.
            result.skeleton.joints[i].inverseBindMatrix = glm::make_mat4(m);
        }
    }

    std::printf("  Skeleton: %d joints\n", jointCount);
}

static void ExtractGltfAnimations(cgltf_data* data, LoadedModel& result,
                                   const NodeJointMap& nodeMap) {
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& anim = data->animations[ai];
        AnimationClip clip;
        clip.name = anim.name ? anim.name : "Clip " + std::to_string(ai);

        // One channel per target node+path. Group into AnimChannels by joint.
        std::unordered_map<int, size_t> jointToChannel;

        for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
            const cgltf_animation_channel& ch = anim.channels[ci];
            if (!ch.target_node || !ch.sampler) continue;

            auto it = nodeMap.find(ch.target_node);
            if (it == nodeMap.end()) continue;
            int ji = it->second;

            // Get or create AnimChannel for this joint.
            size_t chIdx;
            auto jit = jointToChannel.find(ji);
            if (jit == jointToChannel.end()) {
                chIdx = clip.channels.size();
                jointToChannel[ji] = chIdx;
                AnimChannel ac;
                ac.jointIndex = ji;
                clip.channels.push_back(std::move(ac));
            } else {
                chIdx = jit->second;
            }
            AnimChannel& ac = clip.channels[chIdx];

            const cgltf_animation_sampler& samp = *ch.sampler;
            size_t keyCount = samp.input->count;

            // For CUBICSPLINE, each input key maps to 3 output elements:
            // [in-tangent, value, out-tangent].  We only need the value (index 1).
            bool cubic = (samp.interpolation == cgltf_interpolation_type_cubic_spline);
            size_t stride = cubic ? 3 : 1;

            for (size_t k = 0; k < keyCount; ++k) {
                float t = 0.0f;
                cgltf_accessor_read_float(samp.input, k, &t, 1);
                clip.duration = std::max(clip.duration, t);

                // Output index: for cubic, skip in-tangent (+1).
                size_t outIdx = k * stride + (cubic ? 1 : 0);

                switch (ch.target_path) {
                case cgltf_animation_path_type_translation: {
                    float v[3]; cgltf_accessor_read_float(samp.output, outIdx, v, 3);
                    ac.positions.push_back({t, {v[0], v[1], v[2]}});
                    break;
                }
                case cgltf_animation_path_type_rotation: {
                    float v[4]; cgltf_accessor_read_float(samp.output, outIdx, v, 4);
                    // glTF quats are XYZW, glm quats are WXYZ.
                    ac.rotations.push_back({t, glm::quat(v[3], v[0], v[1], v[2])});
                    break;
                }
                case cgltf_animation_path_type_scale: {
                    float v[3]; cgltf_accessor_read_float(samp.output, outIdx, v, 3);
                    ac.scales.push_back({t, {v[0], v[1], v[2]}});
                    break;
                }
                default: break;
                }
            }
        }

        if (!clip.channels.empty()) {
            std::printf("  Animation '%s': %.2fs, %zu channels\n",
                        clip.name.c_str(), clip.duration, clip.channels.size());
            result.clips.push_back(std::move(clip));
        }
    }
}

static LoadedModel LoadGltf(const char* path) {
    LoadedModel result;
    glm::vec3 bmin{kFloatMax}, bmax{-kFloatMax};
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

    // Extract skeleton and animations first (needed for joint mapping).
    NodeJointMap nodeMap;
    ExtractGltfSkeleton(data, result, nodeMap);
    ExtractGltfAnimations(data, result, nodeMap);

    bool skinned = result.skinned;
    int fpv = skinned ? kSkinFloatsPerV : kFloatsPerV;

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        cgltf_mesh& mesh = data->meshes[mi];

        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            cgltf_accessor* posAcc  = nullptr;
            cgltf_accessor* normAcc = nullptr;
            cgltf_accessor* uvAcc   = nullptr;
            cgltf_accessor* jointsAcc = nullptr;
            cgltf_accessor* weightsAcc = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                switch (prim.attributes[ai].type) {
                case cgltf_attribute_type_position: posAcc     = prim.attributes[ai].data; break;
                case cgltf_attribute_type_normal:   normAcc    = prim.attributes[ai].data; break;
                case cgltf_attribute_type_texcoord: uvAcc      = prim.attributes[ai].data; break;
                case cgltf_attribute_type_joints:   jointsAcc  = prim.attributes[ai].data; break;
                case cgltf_attribute_type_weights:  weightsAcc = prim.attributes[ai].data; break;
                default: break;
                }
            }
            if (!posAcc) continue;

            auto vertCount = static_cast<size_t>(posAcc->count);
            std::vector<float> verts(vertCount * fpv, 0.0f);

            for (size_t vi = 0; vi < vertCount; ++vi) {
                float pos[3]={0,0,0}, uv[2]={0,0};
                cgltf_accessor_read_float(posAcc, vi, pos, 3);
                if (uvAcc) cgltf_accessor_read_float(uvAcc, vi, uv, 2);
                ExpandBounds(bmin, bmax, pos[0], pos[1], pos[2]);

                float* v = &verts[vi * fpv];
                v[0]=pos[0]; v[1]=pos[1]; v[2]=pos[2];
                v[6]=uv[0]; v[7]=uv[1];

                if (skinned) {
                    // Default: bone 0, weight 1.
                    v[8]=0; v[9]=0; v[10]=0; v[11]=0;
                    v[12]=1; v[13]=0; v[14]=0; v[15]=0;
                    // Intentional: overwritten below if accessors exist.
                    // But set defaults for verts that lack skin data.
                }
            }

            if (normAcc) {
                for (size_t vi = 0; vi < vertCount; ++vi) {
                    float norm[3]={0,0,1};
                    cgltf_accessor_read_float(normAcc, vi, norm, 3);
                    float* v = &verts[vi * fpv];
                    v[3]=norm[0]; v[4]=norm[1]; v[5]=norm[2];
                }
            }

            // Skinning weights.
            if (skinned && jointsAcc && weightsAcc) {
                for (size_t vi = 0; vi < vertCount; ++vi) {
                    cgltf_uint j[4]={0,0,0,0};
                    float w[4]={0,0,0,0};
                    cgltf_accessor_read_uint(jointsAcc, vi, j, 4);
                    cgltf_accessor_read_float(weightsAcc, vi, w, 4);
                    float* v = &verts[vi * fpv];
                    v[8]  = static_cast<float>(j[0]);
                    v[9]  = static_cast<float>(j[1]);
                    v[10] = static_cast<float>(j[2]);
                    v[11] = static_cast<float>(j[3]);
                    v[12]=w[0]; v[13]=w[1]; v[14]=w[2]; v[15]=w[3];
                }
            }

            std::vector<std::uint32_t> indices;
            if (prim.indices) {
                indices.resize(static_cast<size_t>(prim.indices->count));
                for (size_t ii = 0; ii < indices.size(); ++ii)
                    indices[ii] = static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, ii));
            }

            if (!normAcc) {
                std::printf("  Generating normals for primitive %zu/%zu\n", pi, mi);
                GenerateNormals(verts.data(), vertCount, fpv,
                                indices.empty() ? nullptr : indices.data(), indices.size());
            }

            int snOff = skinned ? kSkinSmoothNormOffset : kSmoothNormOffset;
            ComputeSmoothNormals(verts.data(), vertCount, fpv, snOff);

            SubMesh sm;
            sm.mesh = CreateMesh(
                verts.data(), verts.size() * sizeof(float),
                skinned ? kSkinStride : kStride,
                skinned ? kSkinAttribs : kAttribs,
                skinned ? kSkinAttribCount : kAttribCount,
                static_cast<int>(vertCount),
                indices.empty() ? nullptr : indices.data(),
                static_cast<int>(indices.size()));
            sm.material = ExtractGltfMaterial(prim, modelDir);
            result.subMeshes.push_back(std::move(sm));
        }
    }

    cgltf_free(data);

    if (!result.subMeshes.empty()) {
        result.boundsMin = bmin; result.boundsMax = bmax;
        std::printf("Loaded %zu mesh(es) from %s (glTF)%s\n",
                    result.subMeshes.size(), path, skinned ? " [skinned]" : "");
    }
    return result;
}

// ---------------------------------------------------------------------------
// FBX loader.
// ---------------------------------------------------------------------------

static LoadedModel LoadFbx(const char* path) {
    LoadedModel result;
    glm::vec3 bmin{kFloatMax}, bmax{-kFloatMax};
    auto modelDir = std::filesystem::path(path).parent_path();

    ufbx_load_opts opts{};
    opts.generate_missing_normals = true;

    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path, &opts, &error);
    if (!scene) {
        std::fprintf(stderr, "ufbx: failed to load %s: %s\n", path, error.description.data);
        return result;
    }

    // Detect skinning: look for skin deformers on any mesh.
    ufbx_skin_deformer* skinDef = nullptr;
    for (size_t i = 0; i < scene->skin_deformers.count && !skinDef; ++i)
        skinDef = scene->skin_deformers.data[i];

    // --- Skeleton extraction from skin deformer clusters. ---
    std::unordered_map<const ufbx_node*, int> boneMap;
    if (skinDef && skinDef->clusters.count > 0) {
        int jointCount = std::min(static_cast<int>(skinDef->clusters.count), kMaxJoints);
        result.skeleton.joints.resize(jointCount);
        result.skinned = true;

        for (int i = 0; i < jointCount; ++i) {
            const ufbx_skin_cluster* cl = skinDef->clusters.data[i];
            Joint& j = result.skeleton.joints[i];
            j.name = cl->bone_node ? std::string(cl->bone_node->name.data, cl->bone_node->name.length) : "";
            boneMap[cl->bone_node] = i;

            // Bind-pose local TRS from the bone node.
            if (cl->bone_node) {
                const ufbx_transform& lt = cl->bone_node->local_transform;
                j.bindPosition = glm::vec3(static_cast<float>(lt.translation.x),
                                           static_cast<float>(lt.translation.y),
                                           static_cast<float>(lt.translation.z));
                j.bindRotation = glm::quat(static_cast<float>(lt.rotation.w),
                                           static_cast<float>(lt.rotation.x),
                                           static_cast<float>(lt.rotation.y),
                                           static_cast<float>(lt.rotation.z));
                j.bindScale    = glm::vec3(static_cast<float>(lt.scale.x),
                                           static_cast<float>(lt.scale.y),
                                           static_cast<float>(lt.scale.z));
            }

            // geometry_to_bone is the inverse bind matrix (mesh space -> bone space).
            const ufbx_matrix& m = cl->geometry_to_bone;
            j.inverseBindMatrix = glm::mat4(
                static_cast<float>(m.m00), static_cast<float>(m.m10), static_cast<float>(m.m20), 0.0f,
                static_cast<float>(m.m01), static_cast<float>(m.m11), static_cast<float>(m.m21), 0.0f,
                static_cast<float>(m.m02), static_cast<float>(m.m12), static_cast<float>(m.m22), 0.0f,
                static_cast<float>(m.m03), static_cast<float>(m.m13), static_cast<float>(m.m23), 1.0f);
        }

        // Parent indices from the node hierarchy.
        for (int i = 0; i < jointCount; ++i) {
            const ufbx_node* bone = skinDef->clusters.data[i]->bone_node;
            result.skeleton.joints[i].parent = -1;
            if (bone) {
                const ufbx_node* p = bone->parent;
                while (p) {
                    auto it = boneMap.find(p);
                    if (it != boneMap.end()) { result.skeleton.joints[i].parent = it->second; break; }
                    p = p->parent;
                }
            }
        }
        std::printf("  Skeleton: %d joints\n", jointCount);
    }

    // --- Animation extraction via ufbx bake API. ---
    for (size_t si = 0; si < scene->anim_stacks.count; ++si) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[si];

        ufbx_bake_opts bakeOpts{};
        ufbx_error bakeErr{};
        ufbx_baked_anim* baked = ufbx_bake_anim(scene, stack->anim, &bakeOpts, &bakeErr);
        if (!baked) continue;

        AnimationClip clip;
        clip.name = std::string(stack->name.data, stack->name.length);
        if (clip.name.empty()) clip.name = "Clip " + std::to_string(si);
        clip.duration = static_cast<float>(stack->time_end - stack->time_begin);

        for (size_t ni = 0; ni < baked->nodes.count; ++ni) {
            const ufbx_baked_node& bn = baked->nodes.data[ni];
            // Map baked node back to our joint index.
            const ufbx_node* node = (bn.typed_id < scene->nodes.count)
                ? scene->nodes.data[bn.typed_id] : nullptr;
            if (!node) continue;
            auto it = boneMap.find(node);
            if (it == boneMap.end()) continue;

            AnimChannel ch;
            ch.jointIndex = it->second;

            for (size_t k = 0; k < bn.translation_keys.count; ++k) {
                const auto& key = bn.translation_keys.data[k];
                float t = static_cast<float>(key.time - stack->time_begin);
                ch.positions.push_back({t, {static_cast<float>(key.value.x),
                                            static_cast<float>(key.value.y),
                                            static_cast<float>(key.value.z)}});
            }
            for (size_t k = 0; k < bn.rotation_keys.count; ++k) {
                const auto& key = bn.rotation_keys.data[k];
                float t = static_cast<float>(key.time - stack->time_begin);
                ch.rotations.push_back({t, glm::quat(
                    static_cast<float>(key.value.w), static_cast<float>(key.value.x),
                    static_cast<float>(key.value.y), static_cast<float>(key.value.z))});
            }
            for (size_t k = 0; k < bn.scale_keys.count; ++k) {
                const auto& key = bn.scale_keys.data[k];
                float t = static_cast<float>(key.time - stack->time_begin);
                ch.scales.push_back({t, {static_cast<float>(key.value.x),
                                         static_cast<float>(key.value.y),
                                         static_cast<float>(key.value.z)}});
            }

            if (!ch.positions.empty() || !ch.rotations.empty() || !ch.scales.empty())
                clip.channels.push_back(std::move(ch));
        }

        ufbx_free_baked_anim(baked);

        if (!clip.channels.empty()) {
            std::printf("  Animation '%s': %.2fs, %zu channels\n",
                        clip.name.c_str(), clip.duration, clip.channels.size());
            result.clips.push_back(std::move(clip));
        }
    }

    // --- Mesh extraction. ---
    bool skinned = result.skinned;
    int fpv = skinned ? kSkinFloatsPerV : kFloatsPerV;

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        ufbx_mesh* mesh = scene->meshes.data[mi];

        // Find skin deformer for this mesh.
        ufbx_skin_deformer* meshSkin = nullptr;
        if (skinned) {
            for (size_t di = 0; di < mesh->skin_deformers.count; ++di) {
                meshSkin = mesh->skin_deformers.data[di];
                break;
            }
        }

        size_t maxTriVerts = mesh->max_face_triangles * 3;
        std::vector<uint32_t> triIndices(maxTriVerts);

        auto triangulateFace = [&](ufbx_face face, std::vector<float>& verts) {
            size_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
            for (size_t ti = 0; ti < numTris * 3; ++ti) {
                uint32_t idx = triIndices[ti];

                ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, idx);
                ufbx_vec3 norm{}; norm.y = 1.0;
                if (mesh->vertex_normal.exists)
                    norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, idx);
                ufbx_vec2 uv{};
                if (mesh->vertex_uv.exists)
                    uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, idx);

                float px=static_cast<float>(pos.x), py=static_cast<float>(pos.y), pz=static_cast<float>(pos.z);
                ExpandBounds(bmin, bmax, px, py, pz);

                verts.push_back(px); verts.push_back(py); verts.push_back(pz);
                verts.push_back(static_cast<float>(norm.x));
                verts.push_back(static_cast<float>(norm.y));
                verts.push_back(static_cast<float>(norm.z));
                verts.push_back(static_cast<float>(uv.x));
                verts.push_back(static_cast<float>(uv.y));

                if (skinned) {
                    float ids[4]  = {0, 0, 0, 0};
                    float wts[4]  = {1, 0, 0, 0};
                    uint32_t vertIdx = mesh->vertex_indices.data[idx];
                    if (meshSkin && vertIdx < meshSkin->vertices.count) {
                        const ufbx_skin_vertex& sv = meshSkin->vertices.data[vertIdx];
                        int count = std::min(static_cast<int>(sv.num_weights), 4);
                        float wSum = 0.0f;
                        for (int w = 0; w < count; ++w) {
                            const ufbx_skin_weight& sw = meshSkin->weights.data[sv.weight_begin + w];
                            auto it = boneMap.find(meshSkin->clusters.data[sw.cluster_index]->bone_node);
                            ids[w] = (it != boneMap.end()) ? static_cast<float>(it->second) : 0.0f;
                            wts[w] = static_cast<float>(sw.weight);
                            wSum += wts[w];
                        }
                        if (wSum > 0.0f) {
                            for (int w = 0; w < 4; ++w) wts[w] /= wSum;
                        } else {
                            wts[0] = 1.0f;
                        }
                    }
                    for (int w = 0; w < 4; ++w) verts.push_back(ids[w]);
                    for (int w = 0; w < 4; ++w) verts.push_back(wts[w]);
                }

                // Placeholder for smooth normal (computed after all verts are in).
                verts.push_back(0.0f); verts.push_back(0.0f); verts.push_back(0.0f);
            }
        };

        size_t numParts = mesh->material_parts.count;
        if (numParts == 0) {
            std::vector<float> verts;
            for (size_t fi = 0; fi < mesh->faces.count; ++fi)
                triangulateFace(mesh->faces.data[fi], verts);
            auto vertCount = static_cast<int>(verts.size() / fpv);
            if (vertCount == 0) continue;
            int snOff = skinned ? kSkinSmoothNormOffset : kSmoothNormOffset;
            ComputeSmoothNormals(verts.data(), vertCount, fpv, snOff);
            SubMesh sm;
            sm.mesh = CreateMesh(verts.data(), verts.size() * sizeof(float),
                                 skinned ? kSkinStride : kStride,
                                 skinned ? kSkinAttribs : kAttribs,
                                 skinned ? kSkinAttribCount : kAttribCount, vertCount);
            const ufbx_material* mat = (mesh->materials.count > 0) ? mesh->materials.data[0] : nullptr;
            sm.material = ExtractFbxMaterial(mat, modelDir);
            result.subMeshes.push_back(std::move(sm));
        } else {
            for (size_t pi = 0; pi < numParts; ++pi) {
                const ufbx_mesh_part& part = mesh->material_parts.data[pi];
                if (part.num_faces == 0) continue;
                std::vector<float> verts;
                for (size_t fi = 0; fi < part.face_indices.count; ++fi)
                    triangulateFace(mesh->faces.data[part.face_indices.data[fi]], verts);
                auto vertCount = static_cast<int>(verts.size() / fpv);
                if (vertCount == 0) continue;
                int snOff2 = skinned ? kSkinSmoothNormOffset : kSmoothNormOffset;
                ComputeSmoothNormals(verts.data(), vertCount, fpv, snOff2);
                SubMesh sm;
                sm.mesh = CreateMesh(verts.data(), verts.size() * sizeof(float),
                                     skinned ? kSkinStride : kStride,
                                     skinned ? kSkinAttribs : kAttribs,
                                     skinned ? kSkinAttribCount : kAttribCount, vertCount);
                const ufbx_material* mat = (part.index < mesh->materials.count)
                    ? mesh->materials.data[part.index] : nullptr;
                sm.material = ExtractFbxMaterial(mat, modelDir);
                result.subMeshes.push_back(std::move(sm));
            }
        }
    }

    ufbx_free_scene(scene);

    if (!result.subMeshes.empty()) {
        result.boundsMin = bmin; result.boundsMax = bmax;
        std::printf("Loaded %zu mesh(es) from %s (FBX)%s\n",
                    result.subMeshes.size(), path, skinned ? " [skinned]" : "");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
LoadedModel LoadModel(const char* path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".gltf" || ext == ".glb") return LoadGltf(path);
    if (ext == ".fbx")                   return LoadFbx(path);
    std::fprintf(stderr, "Unsupported model format: %s (use .gltf, .glb, or .fbx)\n", ext.c_str());
    return {};
}
