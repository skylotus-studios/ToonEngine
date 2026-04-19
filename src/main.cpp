// ToonEngine entry point.
//
// Sets up a GLFW window with an OpenGL 4.1 core context, loads shaders
// and demo geometry, and runs a fixed-timestep game loop with camera
// input, shader hot-reload, and toon shading with outlines.

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#undef near
#undef far
#endif

#include "core/animation.h"
#include "core/animator.h"
#include "core/input/action_map.h"
#include "core/input/binding_io.h"
#include "core/input/input_system.h"
#include "core/renderer.h"
#include "core/transform.h"
#include "scene/camera.h"
#include "scene/model_loader.h"
#include "scene/scene.h"
#include "ui/overlay.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// Application state (file-scope, anonymous namespace).
// ---------------------------------------------------------------------------
namespace {

    constexpr float  kFloatMax = std::numeric_limits<float>::max();
    constexpr int    kWindowWidth = 3840;
    constexpr int    kWindowHeight = 2160;
    constexpr char   kWindowTitle[] = "ToonEngine";
    constexpr double kFixedTimestep = 1.0 / 60.0;

    ShaderHandle  gShader{};
    TextureHandle gDefaultTexture{};

    ShaderHandle gToonShader{};
    ShaderHandle gOutlineShader{};

    MeshHandle   gFullscreenVAO{};

    ShaderHandle gGridShader{};
    ShaderHandle gSpriteShader{};
    MeshHandle   gSpriteQuad{};

    ShaderHandle      gShadowShader{};
    FramebufferHandle gShadowFB{};
    TextureHandle     gShadowMap{};
    constexpr int kShadowMapSize = 2048;
    constexpr int kCascadeCount  = 4;
    glm::mat4 gCascadeMatrices[kCascadeCount]{};
    float     gCascadeSplits[kCascadeCount]{};

    Scene          gScene{};
    RenderSettings gSettings{};

    Camera     gCamera{};
    int    gFramebufferW = kWindowWidth;
    int    gFramebufferH = kWindowHeight;


    // -----------------------------------------------------------------------
    // GLFW callbacks.
    // -----------------------------------------------------------------------

    void GlfwErrorCallback(int error, const char* description) {
        std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
    }

    void FramebufferSizeCallback(GLFWwindow*, int width, int height) {
        gFramebufferW = width;
        gFramebufferH = height;
        SetViewport(0, 0, width, height);
    }

    // -----------------------------------------------------------------------
    // Frame logic.
    // -----------------------------------------------------------------------

    void Update(double dt) {
        // Spin "Triangle 2" if present (demo content).
        for (auto& e : gScene.entities) {
            if (e.name == "Triangle 2" && e.transform) {
                e.transform->rotation.z += 45.0f * static_cast<float>(dt);
                break;
            }
        }

        // Tick skeletal animation for all entities.
        float fdt = static_cast<float>(dt);
        for (auto& e : gScene.entities) {
            if (e.skinned)
                AnimatorUpdate(e.animator, e.skeleton, e.clips, fdt);
        }
    }

    void RenderSprite(const Entity& entity, const glm::mat4& vp) {
        BindShader(gSpriteShader);
        SetRenderState({CullMode::None, DepthFunc::Less, BlendMode::Alpha});

        SetUniform("uMVP", vp * entity.worldMatrix);
        SetUniform("uTintColor", entity.spriteTint);

        glm::vec4 uvRect = entity.spriteUVRect;
        if (entity.spriteFlipX) {
            uvRect.x += uvRect.z;
            uvRect.z = -uvRect.z;
        }
        if (entity.spriteFlipY) {
            uvRect.y += uvRect.w;
            uvRect.w = -uvRect.w;
        }
        SetUniform("uUVRect", uvRect);

        TextureHandle tex = (!entity.subMeshes.empty() && entity.subMeshes[0].material.texture)
            ? entity.subMeshes[0].material.texture : gDefaultTexture;
        BindTexture(tex, 0);
        SetUniform("uTexture", 0);

        DrawMesh(gSpriteQuad);

        SetRenderState({CullMode::None, DepthFunc::Less, BlendMode::None});
    }

    void RenderVertexColor(const Entity& entity, const glm::mat4& vp) {
        BindShader(gShader);
        BindTexture(gDefaultTexture, 0);
        SetUniform("uTexture", 0);
        SetUniform("uMVP", vp * entity.worldMatrix);

        for (auto& sm : entity.subMeshes)
            DrawMesh(sm.mesh);
    }

    void UploadLights() {
        int count = 0;
        for (auto& e : gScene.entities) {
            if (!e.light || count >= kMaxLights) continue;
            const Light& L = *e.light;
            std::string idx = std::to_string(count);

            SetUniform(("uLightType[" + idx + "]").c_str(),
                L.type == LightType::Directional ? 0 : 1);

            glm::vec3 posOrDir = (L.type == LightType::Directional)
                ? L.direction : glm::vec3(e.worldMatrix[3]);
            SetUniform(("uLightPosOrDir[" + idx + "]").c_str(), posOrDir);
            SetUniform(("uLightColor[" + idx + "]").c_str(), L.color);
            SetUniform(("uLightIntensity[" + idx + "]").c_str(), L.intensity);
            SetUniform(("uLightRadius[" + idx + "]").c_str(), L.radius);
            ++count;
        }
        SetUniform("uLightCount", count);
    }

    void RenderToon(const Entity& entity, const glm::mat4& vp) {
        if (!gToonShader || !gOutlineShader) return;

        const glm::mat4& model = entity.worldMatrix;
        glm::mat4 mvp = vp * model;

        // Pass 1: toon shading (front faces).
        SetRenderState({CullMode::Back, DepthFunc::Less});
        BindShader(gToonShader);
        SetUniform("uMVP", mvp);
        SetUniform("uModel", model);
        SetUniform("uTexture", 0);

        bool doSkin = entity.skinned && !gSettings.debugDisableSkinning;
        SetUniform("uSkinned", doSkin ? 1 : 0);
        if (doSkin && !entity.animator.jointMatrices.empty()) {
            int count = std::min(static_cast<int>(entity.animator.jointMatrices.size()),
                                 kMaxJoints);
            SetUniformArray("uJoints", entity.animator.jointMatrices.data(), count);
        }

        UploadLights();

        bool shadowOn = gSettings.shadowEnabled && gShadowMap;
        SetUniform("uShadowEnabled", shadowOn ? 1 : 0);
        if (shadowOn) {
            BindTextureArray(gShadowMap, 2);
            SetUniform("uShadowMap", 2);
            SetUniform("uCascadeCount", kCascadeCount);
            SetUniformArray("uLightSpaceMatrices", gCascadeMatrices, kCascadeCount);
            SetUniformArray("uCascadeSplits", gCascadeSplits, kCascadeCount);
            SetUniform("uShadowBias", gSettings.shadowBias);
            SetUniform("uViewMatrix", CameraViewMatrix(gCamera));
        }

        SetUniform("uBandHigh",    gSettings.bandThresholdHigh);
        SetUniform("uBandLow",     gSettings.bandThresholdLow);
        SetUniform("uBrightness",  gSettings.brightIntensity);
        SetUniform("uMid",         gSettings.midIntensity);
        SetUniform("uShadow",      gSettings.shadowIntensity);
        SetUniform("uShadowTint",  gSettings.shadowTint);

        SetUniform("uSpecColor",     gSettings.specColor);
        SetUniform("uSpecThreshold", gSettings.specThreshold);
        SetUniform("uSpecStrength",  gSettings.specStrength);
        SetUniform("uSpecShininess", gSettings.specShininess);

        SetUniform("uViewPos",     gCamera.position);
        SetUniform("uRimColor",    gSettings.rimColor);
        SetUniform("uRimPower",    gSettings.rimPower);
        SetUniform("uRimStrength", gSettings.rimStrength);

        for (auto& sm : entity.subMeshes) {
            BindTexture(sm.material.texture ? sm.material.texture
                : gDefaultTexture, 0);

            bool hasNM = static_cast<bool>(sm.material.normalMap);
            SetUniform("uHasNormalMap", hasNM ? 1 : 0);
            if (hasNM) {
                BindTexture(sm.material.normalMap, 3);
                SetUniform("uNormalMap", 3);
            }

            SetUniform("uBaseColor", sm.material.baseColor);
            DrawMesh(sm.mesh);
        }

        // Pass 2: outlines (back faces extruded along normals).
        SetRenderState({CullMode::Front, DepthFunc::Less});
        BindShader(gOutlineShader);
        SetUniform("uMVP", mvp);
        SetUniform("uOutlineWidth", gSettings.outlineWidth);
        SetUniform("uOutlineColor", gSettings.outlineColor);
        SetUniform("uScreenSpace", gSettings.outlineScreenSpace ? 1 : 0);
        SetUniform("uViewportSize", glm::vec2(
            static_cast<float>(gFramebufferW), static_cast<float>(gFramebufferH)));
        SetUniform("uSkinned", doSkin ? 1 : 0);
        if (doSkin && !entity.animator.jointMatrices.empty()) {
            int count = std::min(static_cast<int>(entity.animator.jointMatrices.size()),
                                 kMaxJoints);
            SetUniformArray("uJoints", entity.animator.jointMatrices.data(), count);
        }

        for (auto& sm : entity.subMeshes)
            DrawMesh(sm.mesh);

        SetRenderState({CullMode::None, DepthFunc::Less});
    }

    // Compute cascade frustum splits and per-cascade light-space matrices.
    void ComputeCascades(const glm::vec3& lightDir, float aspect) {
        float near = gCamera.nearPlane;
        float far  = std::min(gCamera.farPlane, gSettings.shadowDistance);
        constexpr float kLambda = 0.5f;  // log-linear blend

        // Split distances (practical split scheme).
        float splits[kCascadeCount + 1];
        splits[0] = near;
        for (int i = 1; i <= kCascadeCount; ++i) {
            float f = static_cast<float>(i) / kCascadeCount;
            float logS = near * std::pow(far / near, f);
            float linS = near + f * (far - near);
            splits[i] = kLambda * logS + (1.0f - kLambda) * linS;
        }
        for (int i = 0; i < kCascadeCount; ++i)
            gCascadeSplits[i] = splits[i + 1];

        glm::mat4 camView = CameraViewMatrix(gCamera);
        glm::vec3 up = (std::abs(lightDir.y) > 0.99f)
            ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

        for (int c = 0; c < kCascadeCount; ++c) {
            // Build a projection for this cascade's sub-frustum.
            glm::mat4 proj = glm::perspective(
                glm::radians(gCamera.fovY), aspect, splits[c], splits[c + 1]);
            glm::mat4 invVP = glm::inverse(proj * camView);

            // Unproject 8 NDC corners to world space.
            glm::vec3 corners[8];
            int idx = 0;
            for (int x = 0; x <= 1; ++x)
                for (int y = 0; y <= 1; ++y)
                    for (int z = 0; z <= 1; ++z) {
                        glm::vec4 ndc(x*2.0f-1.0f, y*2.0f-1.0f, z*2.0f-1.0f, 1.0f);
                        glm::vec4 w = invVP * ndc;
                        corners[idx++] = glm::vec3(w) / w.w;
                    }

            // Frustum center.
            glm::vec3 center(0.0f);
            for (int i = 0; i < 8; ++i) center += corners[i];
            center /= 8.0f;

            // Light view matrix looking at center from the light direction.
            glm::mat4 lightView = glm::lookAt(
                center + glm::normalize(lightDir) * 10.0f, center, up);

            // AABB of frustum in light space -> ortho bounds.
            float minX = kFloatMax, maxX = -kFloatMax;
            float minY = kFloatMax, maxY = -kFloatMax;
            float minZ = kFloatMax, maxZ = -kFloatMax;
            for (int i = 0; i < 8; ++i) {
                glm::vec4 lc = lightView * glm::vec4(corners[i], 1.0f);
                minX = std::min(minX, lc.x); maxX = std::max(maxX, lc.x);
                minY = std::min(minY, lc.y); maxY = std::max(maxY, lc.y);
                minZ = std::min(minZ, lc.z); maxZ = std::max(maxZ, lc.z);
            }

            // In light view space, objects in front of the light are at
            // negative Z.  glm::ortho(l,r,b,t,near,far) clips to the range
            // [-far, -near], so we negate and expand to catch shadow casters
            // that are outside the camera frustum but between it and the light.
            float nearClip = -maxZ;
            float farClip  = -minZ;
            // Expand far to capture casters behind the frustum.
            farClip = std::max(farClip, nearClip + 1.0f);
            farClip += 20.0f;
            if (nearClip > 0.1f) nearClip = 0.1f;

            gCascadeMatrices[c] = glm::ortho(minX, maxX, minY, maxY, nearClip, farClip)
                                * lightView;
        }
    }

    void RenderShadowPass(float aspect) {
        glm::vec3 lightDir{0.3f, 1.0f, 0.5f};
        for (auto& e : gScene.entities) {
            if (e.light && e.light->type == LightType::Directional) {
                lightDir = e.light->direction;
                break;
            }
        }
        lightDir = glm::normalize(lightDir);

        ComputeCascades(lightDir, aspect);

        BindFramebuffer(gShadowFB);
        SetViewport(0, 0, kShadowMapSize, kShadowMapSize);
        BindShader(gShadowShader);
        SetRenderState({CullMode::Front, DepthFunc::Less});

        for (int c = 0; c < kCascadeCount; ++c) {
            SetFramebufferLayer(gShadowFB, c);
            Clear(false, true);

            for (auto& entity : gScene.entities) {
                if (entity.light || entity.subMeshes.empty()) continue;
                if (entity.shading != ShadingMode::Toon) continue;

                SetUniform("uLightMVP", gCascadeMatrices[c] * entity.worldMatrix);

                bool doSkin = entity.skinned && !gSettings.debugDisableSkinning;
                SetUniform("uSkinned", doSkin ? 1 : 0);
                if (doSkin && !entity.animator.jointMatrices.empty()) {
                    int count = std::min(static_cast<int>(entity.animator.jointMatrices.size()),
                                         kMaxJoints);
                    SetUniformArray("uJoints", entity.animator.jointMatrices.data(), count);
                }

                for (auto& sm : entity.subMeshes)
                    DrawMesh(sm.mesh);
            }
        }

        SetRenderState({CullMode::None, DepthFunc::Less});
        BindFramebuffer({});
        SetViewport(0, 0, gFramebufferW, gFramebufferH);
    }

    void Render(double /*alpha*/) {
        float aspect = (gFramebufferH > 0)
            ? static_cast<float>(gFramebufferW) / static_cast<float>(gFramebufferH)
            : 1.0f;

        UpdateSceneTransforms(gScene);

        if (gSettings.shadowEnabled && gShadowShader && gShadowFB)
            RenderShadowPass(aspect);

        SetClearColor(gSettings.clearColor.r, gSettings.clearColor.g,
                      gSettings.clearColor.b);
        Clear(true, true);

        glm::mat4 vp = CameraProjectionMatrix(gCamera, aspect)
            * CameraViewMatrix(gCamera);

        // Grid + sky background.
        if (gSettings.gridEnabled && gGridShader) {
            BindShader(gGridShader);
            SetUniform("uInvViewProj", glm::inverse(vp));
            SetUniform("uCameraPos",   gCamera.position);
            SetUniform("uSkyTop",      gSettings.skyTop);
            SetUniform("uSkyBottom",   gSettings.skyBottom);
            SetUniform("uGridColor",   gSettings.gridColor);
            SetUniform("uGridScale",   gSettings.gridScale);
            SetUniform("uGridFade",    gSettings.gridFade);
            SetUniform("uNear",        gCamera.nearPlane);
            SetUniform("uFar",         gCamera.farPlane);

            SetRenderState({CullMode::None, DepthFunc::Always});
            DrawMesh(gFullscreenVAO);
            SetRenderState({CullMode::None, DepthFunc::Less});
        }

        // Opaque pass: vertex-colored + toon entities.
        for (auto& entity : gScene.entities) {
            if (entity.light) continue;
            switch (entity.shading) {
            case ShadingMode::VertexColor: RenderVertexColor(entity, vp); break;
            case ShadingMode::Toon:        RenderToon(entity, vp);        break;
            case ShadingMode::Sprite:      break;  // deferred to sorted sprite pass
            }
        }

        // Transparent pass: sprites sorted back-to-front for correct alpha.
        {
            glm::vec3 camFront = CameraFront(gCamera);
            std::vector<int> spriteIndices;
            for (int i = 0; i < static_cast<int>(gScene.entities.size()); ++i) {
                if (gScene.entities[i].shading == ShadingMode::Sprite && !gScene.entities[i].light)
                    spriteIndices.push_back(i);
            }
            std::sort(spriteIndices.begin(), spriteIndices.end(),
                [&](int a, int b) {
                    float da = glm::dot(glm::vec3(gScene.entities[a].worldMatrix[3]) - gCamera.position, camFront);
                    float db = glm::dot(glm::vec3(gScene.entities[b].worldMatrix[3]) - gCamera.position, camFront);
                    return da > db;  // far to near
                });
            for (int i : spriteIndices)
                RenderSprite(gScene.entities[i], vp);
        }
    }

} // namespace

// ---------------------------------------------------------------------------
// Entry point.
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    glfwSetErrorCallback(GlfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    // Request OpenGL 4.1 core — the highest version macOS supports.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Required on macOS (gives a legacy 2.1 context without it); harmless on Windows.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    #ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    #endif
    GLFWwindow* window = glfwCreateWindow(
        kWindowWidth, kWindowHeight, kWindowTitle, nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Dark title bar (Windows 10 1809+ / Windows 11).
#ifdef _WIN32
    {
        HWND hwnd = glfwGetWin32Window(window);
        BOOL useDark = TRUE;
        // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
        DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
    }
#endif

    // Set window icon (taskbar / alt-tab).
    {
        int w, h, ch;
        unsigned char* px = stbi_load("assets/icon.png", &w, &h, &ch, 4);
        if (px) {
            GLFWimage img{w, h, px};
            glfwSetWindowIcon(window, 1, &img);
            stbi_image_free(px);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!RendererInit(reinterpret_cast<void*(*)(const char*)>(glfwGetProcAddress))) {
        std::fprintf(stderr, "RendererInit failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Register callbacks. Input::Init installs key/mouse/scroll/gamepad
    // callbacks — call it BEFORE OverlayInit so ImGui can chain them.
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    Input::Init(window);

    RegisterDefaultEditorBindings();
    {
        InputContext fileCtx;
        fileCtx.name = "editor";
        if (BindingIO::Load("assets/input.json", fileCtx)) {
            PopContext("editor");
            PushContext(std::move(fileCtx));
        }
    }

    // Sync framebuffer dimensions (may differ from window size on HiDPI).
    {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        gFramebufferW = fbw;
        gFramebufferH = fbh;
        SetViewport(0, 0, fbw, fbh);
    }

    OverlayInit(window);

    // -----------------------------------------------------------------------
    // Resource loading.
    // -----------------------------------------------------------------------
    gShader = LoadShader("assets/shaders/triangle.vert",
        "assets/shaders/triangle.frag");
    if (!gShader) {
        std::fprintf(stderr, "Failed to load triangle shaders\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    gDefaultTexture = CreateWhiteTexture();

    // Scene root must exist before any child entity is appended so parent
    // indices (defaulting to 0) resolve correctly.
    EnsureSceneRoot(gScene);

    // Demo triangle vertex data: position (vec3) + color (vec3) + texcoord (vec2).
    // clang-format off
    float triVerts[] = {
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.5f, 1.0f,
    };
    VertexAttrib triAttribs[] = {
        {3, AttribType::Float, 0},
        {3, AttribType::Float, 3 * sizeof(float)},
        {2, AttribType::Float, 6 * sizeof(float)},
    };
    // clang-format on

    // Create three demo triangle entities.
    struct DemoTriangle { const char* name; Transform transform; };
    // clang-format off
    DemoTriangle demoTriangles[] = {
        { "Triangle 1", {{ 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f,   0.0f}, {1.0f, 1.0f, 1.0f}} },
        { "Triangle 2", {{ 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f,  45.0f}, {0.8f, 0.8f, 0.8f}} },
        { "Triangle 3", {{-2.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -30.0f}, {1.2f, 1.2f, 1.2f}} },
    };
    // clang-format on

    for (auto& dt : demoTriangles) {
        Entity e;
        e.name      = dt.name;
        e.transform.emplace(dt.transform);
        e.shading   = ShadingMode::VertexColor;

        SubMesh sm;
        sm.mesh = CreateMesh(triVerts, sizeof(triVerts), 8 * sizeof(float),
                             triAttribs, 3, 3);
        e.subMeshes.push_back(std::move(sm));
        gScene.entities.push_back(std::move(e));
    }

    // Default scene light — directional "sun."
    {
        Entity e;
        e.name = "Sun";
        e.light = Light{};
        gScene.entities.push_back(std::move(e));
    }

    // Test quad: a 2D plane with a checkerboard texture + procedural normal
    // map.  Uses the Toon shading pipeline to verify stb_image textures,
    // normal mapping, specular, rim, etc. on flat geometry.
    {
        // Unit quad: two triangles, model vertex layout (11 floats per vert).
        // pos(3) + norm(3) + uv(2) + smoothNorm(3)
        // Normal and smooth normal both point +Z (facing camera).
        // clang-format off
        float qv[] = {
            // pos              normal          uv        smooth normal
            -0.5f, -0.5f, 0.0f,  0,0,1,  0,0,  0,0,1,
             0.5f, -0.5f, 0.0f,  0,0,1,  1,0,  0,0,1,
             0.5f,  0.5f, 0.0f,  0,0,1,  1,1,  0,0,1,
            -0.5f, -0.5f, 0.0f,  0,0,1,  0,0,  0,0,1,
             0.5f,  0.5f, 0.0f,  0,0,1,  1,1,  0,0,1,
            -0.5f,  0.5f, 0.0f,  0,0,1,  0,1,  0,0,1,
        };
        VertexAttrib qAttribs[] = {
            {3, AttribType::Float, 0},
            {3, AttribType::Float, 3  * sizeof(float)},
            {2, AttribType::Float, 6  * sizeof(float)},
            {3, AttribType::Float, 8  * sizeof(float), /*location=*/5},
        };
        // clang-format on

        Entity e;
        e.name    = "Test Quad";
        e.shading = ShadingMode::Toon;
        e.transform.emplace();
        e.transform->position = {0.0f, 0.0f, -1.0f};

        SubMesh sm;
        sm.mesh = CreateMesh(qv, sizeof(qv), 11 * sizeof(float), qAttribs, 4, 6);
        sm.material.texture   = CreateCheckerTexture();
        sm.material.normalMap = CreateTestNormalMap();
        e.subMeshes.push_back(std::move(sm));
        gScene.entities.push_back(std::move(e));
    }

    // Toon + outline shaders. model.vert is reused for the toon pass since
    // it already transforms normals to world space.
    gToonShader = LoadShader("assets/shaders/model.vert", "assets/shaders/toon.frag");
    if (!gToonShader) std::fprintf(stderr, "Failed to load toon shaders\n");

    gOutlineShader = LoadShader("assets/shaders/outline.vert", "assets/shaders/outline.frag");
    if (!gOutlineShader) std::fprintf(stderr, "Failed to load outline shaders\n");

    gShadowShader = LoadShader("assets/shaders/shadow.vert", "assets/shaders/shadow.frag");
    if (!gShadowShader) std::fprintf(stderr, "Failed to load shadow shaders\n");

    gGridShader = LoadShader("assets/shaders/fullscreen.vert", "assets/shaders/grid.frag");
    if (!gGridShader) std::fprintf(stderr, "Failed to load grid shaders\n");

    gSpriteShader = LoadShader("assets/shaders/sprite.vert", "assets/shaders/sprite.frag");
    if (!gSpriteShader) std::fprintf(stderr, "Failed to load sprite shaders\n");

    gShadowFB = CreateFramebuffer({kShadowMapSize, kShadowMapSize,
                                   FBAttachment::DepthArray, kCascadeCount});
    gShadowMap = GetFramebufferTexture(gShadowFB);

    gFullscreenVAO = CreateFullscreenTriangle();

    // Shared unit quad for all sprites: pos(3) + uv(2), 6 vertices.
    {
        // clang-format off
        float sv[] = {
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
             0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
        };
        VertexAttrib sa[] = {
            {3, AttribType::Float, 0},
            {2, AttribType::Float, 3 * sizeof(float)},
        };
        // clang-format on
        gSpriteQuad = CreateMesh(sv, sizeof(sv), 5 * sizeof(float), sa, 2, 6);
    }

    // Load a model if a path was passed on the command line.
    // Normalize: scale so the model's largest axis fits in 1 unit, centered
    // at the origin.  1 engine unit = 1 world unit.  Camera frames the
    // 1-unit cube from a fixed, predictable position.
    if (argc > 1) {
        LoadedModel loaded = LoadModel(argv[1]);

        if (loaded.subMeshes.empty()) {
            std::fprintf(stderr, "Failed to load model: %s\n", argv[1]);
        } else {
            glm::vec3 center = (loaded.boundsMin + loaded.boundsMax) * 0.5f;
            glm::vec3 extent = loaded.boundsMax - loaded.boundsMin;
            float maxExtent = std::max({ extent.x, extent.y, extent.z });
            float s = (maxExtent > 0.0f) ? 1.0f / maxExtent : 1.0f;

            Entity e;
            e.name      = std::filesystem::path(argv[1]).filename().string();
            e.modelPath = argv[1];
            e.shading   = ShadingMode::Toon;
            e.transform.emplace();
            e.transform->scale    = glm::vec3(s);
            e.transform->position = -center * s;
            e.subMeshes = std::move(loaded.subMeshes);
            e.skeleton  = std::move(loaded.skeleton);
            e.clips     = std::move(loaded.clips);
            e.skinned   = loaded.skinned;
            if (e.skinned && !e.clips.empty()) {
                AnimatorSetClip(e.animator, e.skeleton, 0);
                AnimatorUpdate(e.animator, e.skeleton, e.clips, 0.0f);
            }
            gScene.entities.push_back(std::move(e));

            // Camera frames the 1-unit model: 3 units back, slightly above
            // center, looking slightly down. Works for any normalized model.
            gCamera.pivot    = glm::vec3(0.0f);
            gCamera.position = glm::vec3(0.0f, 0.4f, 2.5f);
            gCamera.pitch    = -5.0f;
            gCamera.moveSpeed = 2.0f;

            std::printf("Model: center=(%.1f,%.1f,%.1f) extent=%.1f scale=%.4f\n",
                center.x, center.y, center.z, maxExtent, s);
        }
    }

    // -----------------------------------------------------------------------
    // Main loop — fixed timestep with variable-rate rendering.
    // See: "Fix Your Timestep" (Glenn Fiedler).
    // -----------------------------------------------------------------------
    using Clock = std::chrono::high_resolution_clock;
    auto   prev = Clock::now();
    double accumulator = 0.0;

    while (!glfwWindowShouldClose(window)) {
        // Snapshot previous input state, then pump GLFW events so callbacks
        // update current state. Order matters: BeginFrame before PollEvents
        // so WasPressed detects the transition correctly.
        Input::BeginFrame();
        glfwPollEvents();

        const auto   now = Clock::now();
        const double frame = std::chrono::duration<double>(now - prev).count();
        prev = now;

        // Clamp to avoid spiral-of-death after hitches / breakpoints.
        accumulator += (frame > 0.25 ? 0.25 : frame);

        while (accumulator >= kFixedTimestep) {
            Update(kFixedTimestep);
            accumulator -= kFixedTimestep;
        }

        // Camera input — polling API auto-gates when ImGui is capturing.
        {
            constexpr float kMaxDelta = 50.0f;

            glm::dvec2 scroll = Input::ScrollDelta();
            if (scroll.y != 0.0)
                CameraZoom(gCamera, static_cast<float>(scroll.y));

            bool rightDown  = Input::IsMouseDown(MouseButton::Right);
            bool middleDown = Input::IsMouseDown(MouseButton::Middle);

            // Skip delta on the first frame of a press to avoid a jump when
            // the cursor was elsewhere before the button went down.
            bool firstRight  = Input::WasMousePressed(MouseButton::Right);
            bool firstMiddle = Input::WasMousePressed(MouseButton::Middle);

            if (rightDown && !firstRight) {
                glm::dvec2 d = Input::MouseDelta();
                float dx = std::clamp(static_cast<float>( d.x), -kMaxDelta, kMaxDelta);
                float dy = std::clamp(static_cast<float>(-d.y), -kMaxDelta, kMaxDelta);
                CameraOrbit(gCamera, dx, dy);
            }
            if (middleDown && !firstMiddle) {
                glm::dvec2 d = Input::MouseDelta();
                float dx = std::clamp(static_cast<float>( d.x), -kMaxDelta, kMaxDelta);
                float dy = std::clamp(static_cast<float>(-d.y), -kMaxDelta, kMaxDelta);
                CameraPan(gCamera, dx, dy);
            }

            if (rightDown)
                CameraFly(gCamera, static_cast<float>(frame));

            // Gamepad orbit via right stick.
            float gpOrbitX = GetAxis("camera.orbit.x");
            float gpOrbitY = GetAxis("camera.orbit.y");
            if (gpOrbitX != 0.0f || gpOrbitY != 0.0f)
                CameraOrbit(gCamera, gpOrbitX * 3.0f, -gpOrbitY * 3.0f);

            if (WasActionPressed("camera.focus")) {
                glm::vec3 target{0.0f};
                if (gScene.selected >= 0 &&
                    gScene.selected < static_cast<int>(gScene.entities.size())) {
                    const Entity& sel = gScene.entities[gScene.selected];
                    target = glm::vec3(sel.worldMatrix[3]);
                }
                CameraFocus(gCamera, target);
            }

            if (!rightDown) {
                if (WasActionPressed("gizmo.translate")) gSettings.gizmoOp = 0;
                if (WasActionPressed("gizmo.rotate"))    gSettings.gizmoOp = 1;
                if (WasActionPressed("gizmo.scale"))     gSettings.gizmoOp = 2;
            }
        }

        if (WasActionPressed("app.quit"))
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        ReloadIfChanged(gShader);
        ReloadIfChanged(gToonShader);
        ReloadIfChanged(gOutlineShader);
        ReloadIfChanged(gShadowShader);
        ReloadIfChanged(gGridShader);
        ReloadIfChanged(gSpriteShader);

        const double alpha = accumulator / kFixedTimestep;
        Render(alpha);

        // ImGui overlay (rendered after the scene, on top).
        OverlayNewFrame();
        float fps = (frame > 0.0) ? static_cast<float>(1.0 / frame) : 0.0f;
        bool captured = OverlayRender(gSettings, gScene, gCamera, gDefaultTexture, fps);
        Input::SetCaptured(captured, captured);

        glfwSwapBuffers(window);
    }

    // -----------------------------------------------------------------------
    // Cleanup — reverse order of creation.
    // -----------------------------------------------------------------------
    OverlayShutdown();
    Input::Shutdown();
    DestroyScene(gScene);
    DestroyMesh(gFullscreenVAO);
    DestroyMesh(gSpriteQuad);
    DestroyFramebuffer(gShadowFB);
    DestroyShader(gSpriteShader);
    DestroyShader(gGridShader);
    DestroyShader(gShadowShader);
    DestroyShader(gOutlineShader);
    DestroyShader(gToonShader);
    DestroyTexture(gDefaultTexture);
    DestroyShader(gShader);
    RendererShutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
