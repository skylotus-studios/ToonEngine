// ToonEngine entry point.
//
// Sets up a GLFW window with an OpenGL 4.1 core context, loads shaders
// and demo geometry, and runs a fixed-timestep game loop with camera
// input, shader hot-reload, and toon shading with outlines.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "core/animation.h"
#include "core/animator.h"
#include "core/mesh.h"
#include "core/shader.h"
#include "core/texture.h"
#include "core/transform.h"
#include "scene/camera.h"
#include "scene/model_loader.h"
#include "scene/scene.h"
#include "ui/overlay.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

    ShaderProgram gShader{};
    Texture       gDefaultTexture{};

    // Toon rendering shaders.
    ShaderProgram gToonShader{};
    ShaderProgram gOutlineShader{};

    // Sobel edge detection post-process.
    ShaderProgram gEdgeShader{};
    GLuint gFBO        = 0;
    GLuint gColorTex   = 0;
    GLuint gDepthTex   = 0;
    GLuint gFullscreenVAO = 0;
    int    gFBOWidth   = 0;
    int    gFBOHeight  = 0;

    // Grid / sky environment.
    ShaderProgram gGridShader{};

    // Cascaded shadow mapping.
    ShaderProgram gShadowShader{};
    GLuint gShadowFBO    = 0;
    GLuint gShadowMap    = 0;  // GL_TEXTURE_2D_ARRAY (one layer per cascade)
    constexpr int kShadowMapSize = 2048;
    constexpr int kCascadeCount  = 4;
    glm::mat4 gCascadeMatrices[kCascadeCount]{};
    float     gCascadeSplits[kCascadeCount]{};

    Scene          gScene{};
    RenderSettings gSettings{};
    bool           gOverlayCapturing = false;

    Camera gCamera{};
    int    gFramebufferW = kWindowWidth;
    int    gFramebufferH = kWindowHeight;

    // Mouse state for right-click look mode.
    double gLastMouseX = 0.0;
    double gLastMouseY = 0.0;
    bool   gRightMouseHeld = false;


    // -----------------------------------------------------------------------
    // FBO for post-processing (Sobel edge detection).
    // -----------------------------------------------------------------------
    void CreateOrResizeFBO(int w, int h) {
        if (w == gFBOWidth && h == gFBOHeight && gFBO) return;
        if (w <= 0 || h <= 0) return;

        if (gFBO)      { glDeleteFramebuffers(1, &gFBO);  gFBO = 0; }
        if (gColorTex) { glDeleteTextures(1, &gColorTex);  gColorTex = 0; }
        if (gDepthTex) { glDeleteTextures(1, &gDepthTex);  gDepthTex = 0; }

        glGenFramebuffers(1, &gFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, gFBO);

        glGenTextures(1, &gColorTex);
        glBindTexture(GL_TEXTURE_2D, gColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, gColorTex, 0);

        glGenTextures(1, &gDepthTex);
        glBindTexture(GL_TEXTURE_2D, gDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, gDepthTex, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "FBO incomplete (%dx%d)\n", w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gFBOWidth  = w;
        gFBOHeight = h;
    }

    void CreateShadowFBO() {
        if (gShadowFBO) return;

        glGenTextures(1, &gShadowMap);
        glBindTexture(GL_TEXTURE_2D_ARRAY, gShadowMap);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                     kShadowMapSize, kShadowMapSize, kCascadeCount,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glGenFramebuffers(1, &gShadowFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, gShadowFBO);
        // Attach layer 0 initially; we re-attach per-cascade in the render loop.
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  gShadowMap, 0, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "Shadow FBO incomplete\n");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // -----------------------------------------------------------------------
    // GLFW callbacks.
    // -----------------------------------------------------------------------

    void GlfwErrorCallback(int error, const char* description) {
        std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
    }

    void FramebufferSizeCallback(GLFWwindow*, int width, int height) {
        gFramebufferW = width;
        gFramebufferH = height;
        glViewport(0, 0, width, height);
        CreateOrResizeFBO(width, height);
    }

    // Right-click toggles cursor capture for camera look mode.
    // Ignored when ImGui wants the mouse (interacting with overlay widgets).
    void MouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
        if (gOverlayCapturing && action == GLFW_PRESS) return;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            gRightMouseHeld = (action == GLFW_PRESS);
            if (gRightMouseHeld) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                glfwGetCursorPos(window, &gLastMouseX, &gLastMouseY);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
    }

    // Feed mouse deltas into the camera. Y is inverted because screen Y
    // increases downward but pitch-up should be positive.
    void CursorPosCallback(GLFWwindow*, double x, double y) {
        if (!gRightMouseHeld) return;
        auto dx = static_cast<float>(x - gLastMouseX);
        auto dy = static_cast<float>(gLastMouseY - y);
        gLastMouseX = x;
        gLastMouseY = y;
        CameraProcessMouse(gCamera, dx, dy);
    }

    // -----------------------------------------------------------------------
    // GL debug callback (only active when KHR_debug is available; not on macOS).
    // -----------------------------------------------------------------------
    void APIENTRY GlDebugCallback(GLenum source, GLenum type, GLuint id,
        GLenum severity, GLsizei /*length*/,
        const GLchar* message, const void* /*user*/) {
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

        const char* src = "?";
        switch (source) {
        case GL_DEBUG_SOURCE_API:             src = "API";        break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   src = "WINDOW";     break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: src = "SHADER";     break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     src = "THIRDPARTY"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     src = "APP";        break;
        case GL_DEBUG_SOURCE_OTHER:           src = "OTHER";      break;
        }
        const char* typ = "?";
        switch (type) {
        case GL_DEBUG_TYPE_ERROR:               typ = "ERROR";       break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typ = "DEPRECATED";  break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typ = "UB";          break;
        case GL_DEBUG_TYPE_PORTABILITY:         typ = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         typ = "PERF";        break;
        case GL_DEBUG_TYPE_MARKER:              typ = "MARKER";      break;
        case GL_DEBUG_TYPE_OTHER:               typ = "OTHER";       break;
        }
        const char* sev = "?";
        switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:   sev = "HIGH";   break;
        case GL_DEBUG_SEVERITY_MEDIUM: sev = "MEDIUM"; break;
        case GL_DEBUG_SEVERITY_LOW:    sev = "LOW";    break;
        }
        std::fprintf(stderr, "[GL %s/%s/%s #%u] %s\n", sev, typ, src, id, message);
    }

    // -----------------------------------------------------------------------
    // Frame logic.
    // -----------------------------------------------------------------------

    void Update(double dt) {
        // Spin the second demo triangle (if it exists).
        if (gScene.entities.size() > 1)
            gScene.entities[1].transform.rotation.z += 45.0f * static_cast<float>(dt);

        // Tick skeletal animation for all entities.
        float fdt = static_cast<float>(dt);
        for (auto& e : gScene.entities) {
            if (e.skinned)
                AnimatorUpdate(e.animator, e.skeleton, e.clips, fdt);
        }
    }

    void RenderVertexColor(const Entity& entity, const glm::mat4& vp) {
        glUseProgram(gShader.id);
        BindTexture(gDefaultTexture, 0);
        glUniform1i(glGetUniformLocation(gShader.id, "uTexture"), 0);

        glm::mat4 mvp = vp * TransformMatrix(entity.transform);
        glUniformMatrix4fv(glGetUniformLocation(gShader.id, "uMVP"),
            1, GL_FALSE, glm::value_ptr(mvp));

        for (auto& sm : entity.subMeshes)
            DrawMesh(sm.mesh);
    }

    // Upload all scene lights to a shader program.
    void UploadLights(GLuint prog) {
        int count = 0;
        for (auto& e : gScene.entities) {
            if (!e.light || count >= kMaxLights) continue;
            const Light& L = *e.light;
            std::string idx = std::to_string(count);

            glUniform1i(glGetUniformLocation(prog,
                ("uLightType[" + idx + "]").c_str()),
                L.type == LightType::Directional ? 0 : 1);

            glm::vec3 posOrDir = (L.type == LightType::Directional)
                ? L.direction : e.transform.position;
            glUniform3fv(glGetUniformLocation(prog,
                ("uLightPosOrDir[" + idx + "]").c_str()),
                1, glm::value_ptr(posOrDir));

            glUniform3fv(glGetUniformLocation(prog,
                ("uLightColor[" + idx + "]").c_str()),
                1, glm::value_ptr(L.color));
            glUniform1f(glGetUniformLocation(prog,
                ("uLightIntensity[" + idx + "]").c_str()), L.intensity);
            glUniform1f(glGetUniformLocation(prog,
                ("uLightRadius[" + idx + "]").c_str()), L.radius);
            ++count;
        }
        glUniform1i(glGetUniformLocation(prog, "uLightCount"), count);
    }

    void RenderToon(const Entity& entity, const glm::mat4& vp) {
        if (!gToonShader.id || !gOutlineShader.id) return;

        glm::mat4 model = TransformMatrix(entity.transform);
        glm::mat4 mvp   = vp * model;

        glEnable(GL_CULL_FACE);

        // Pass 1: toon shading (front faces).
        glCullFace(GL_BACK);
        glUseProgram(gToonShader.id);
        glUniformMatrix4fv(glGetUniformLocation(gToonShader.id, "uMVP"),
            1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(gToonShader.id, "uModel"),
            1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(glGetUniformLocation(gToonShader.id, "uTexture"), 0);

        // Skeletal animation (DEBUG: override via gSettings.debugDisableSkinning).
        bool doSkin = entity.skinned && !gSettings.debugDisableSkinning;
        glUniform1i(glGetUniformLocation(gToonShader.id, "uSkinned"),
            doSkin ? 1 : 0);
        if (doSkin && !entity.animator.jointMatrices.empty()) {
            int count = std::min(static_cast<int>(entity.animator.jointMatrices.size()),
                                 kMaxJoints);
            glUniformMatrix4fv(glGetUniformLocation(gToonShader.id, "uJoints"),
                count, GL_FALSE,
                glm::value_ptr(entity.animator.jointMatrices[0]));
        }

        UploadLights(gToonShader.id);

        // Cascaded shadow maps.
        bool shadowOn = gSettings.shadowEnabled && gShadowMap;
        glUniform1i(glGetUniformLocation(gToonShader.id, "uShadowEnabled"),
            shadowOn ? 1 : 0);
        if (shadowOn) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D_ARRAY, gShadowMap);
            glUniform1i(glGetUniformLocation(gToonShader.id, "uShadowMap"), 2);
            glUniform1i(glGetUniformLocation(gToonShader.id, "uCascadeCount"), kCascadeCount);
            glUniformMatrix4fv(glGetUniformLocation(gToonShader.id, "uLightSpaceMatrices"),
                kCascadeCount, GL_FALSE, glm::value_ptr(gCascadeMatrices[0]));
            glUniform1fv(glGetUniformLocation(gToonShader.id, "uCascadeSplits"),
                kCascadeCount, gCascadeSplits);
            glUniform1f(glGetUniformLocation(gToonShader.id, "uShadowBias"),
                gSettings.shadowBias);
            glUniformMatrix4fv(glGetUniformLocation(gToonShader.id, "uViewMatrix"),
                1, GL_FALSE, glm::value_ptr(CameraViewMatrix(gCamera)));
            glActiveTexture(GL_TEXTURE0);
        }

        glUniform1f(glGetUniformLocation(gToonShader.id, "uBandHigh"),
            gSettings.bandThresholdHigh);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uBandLow"),
            gSettings.bandThresholdLow);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uBrightness"),
            gSettings.brightIntensity);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uMid"),
            gSettings.midIntensity);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uShadow"),
            gSettings.shadowIntensity);
        glUniform3fv(glGetUniformLocation(gToonShader.id, "uShadowTint"),
            1, glm::value_ptr(gSettings.shadowTint));

        // Specular highlight band.
        glUniform3fv(glGetUniformLocation(gToonShader.id, "uSpecColor"),
            1, glm::value_ptr(gSettings.specColor));
        glUniform1f(glGetUniformLocation(gToonShader.id, "uSpecThreshold"),
            gSettings.specThreshold);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uSpecStrength"),
            gSettings.specStrength);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uSpecShininess"),
            gSettings.specShininess);

        // Rim lighting.
        glUniform3fv(glGetUniformLocation(gToonShader.id, "uViewPos"),
            1, glm::value_ptr(gCamera.position));
        glUniform3fv(glGetUniformLocation(gToonShader.id, "uRimColor"),
            1, glm::value_ptr(gSettings.rimColor));
        glUniform1f(glGetUniformLocation(gToonShader.id, "uRimPower"),
            gSettings.rimPower);
        glUniform1f(glGetUniformLocation(gToonShader.id, "uRimStrength"),
            gSettings.rimStrength);

        GLint baseColorLoc  = glGetUniformLocation(gToonShader.id, "uBaseColor");
        GLint hasNormalLoc  = glGetUniformLocation(gToonShader.id, "uHasNormalMap");
        GLint normalMapLoc  = glGetUniformLocation(gToonShader.id, "uNormalMap");
        for (auto& sm : entity.subMeshes) {
            BindTexture(sm.material.texture.id ? sm.material.texture
                : gDefaultTexture, 0);

            bool hasNM = sm.material.normalMap.id != 0;
            glUniform1i(hasNormalLoc, hasNM ? 1 : 0);
            if (hasNM) {
                BindTexture(sm.material.normalMap, 3);
                glUniform1i(normalMapLoc, 3);
            }

            glUniform4fv(baseColorLoc, 1, glm::value_ptr(sm.material.baseColor));
            DrawMesh(sm.mesh);
        }

        // Pass 2: outlines (back faces extruded along normals).
        glCullFace(GL_FRONT);
        glUseProgram(gOutlineShader.id);
        glUniformMatrix4fv(glGetUniformLocation(gOutlineShader.id, "uMVP"),
            1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1f(glGetUniformLocation(gOutlineShader.id, "uOutlineWidth"),
            gSettings.outlineWidth);
        glUniform4fv(glGetUniformLocation(gOutlineShader.id, "uOutlineColor"),
            1, glm::value_ptr(gSettings.outlineColor));
        glUniform1i(glGetUniformLocation(gOutlineShader.id, "uScreenSpace"),
            gSettings.outlineScreenSpace ? 1 : 0);
        glUniform2f(glGetUniformLocation(gOutlineShader.id, "uViewportSize"),
            static_cast<float>(gFramebufferW), static_cast<float>(gFramebufferH));
        glUniform1i(glGetUniformLocation(gOutlineShader.id, "uSkinned"),
            doSkin ? 1 : 0);
        if (doSkin && !entity.animator.jointMatrices.empty()) {
            int count = std::min(static_cast<int>(entity.animator.jointMatrices.size()),
                                 kMaxJoints);
            glUniformMatrix4fv(glGetUniformLocation(gOutlineShader.id, "uJoints"),
                count, GL_FALSE,
                glm::value_ptr(entity.animator.jointMatrices[0]));
        }

        for (auto& sm : entity.subMeshes)
            DrawMesh(sm.mesh);

        glDisable(GL_CULL_FACE);
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

        glBindFramebuffer(GL_FRAMEBUFFER, gShadowFBO);
        glViewport(0, 0, kShadowMapSize, kShadowMapSize);
        glUseProgram(gShadowShader.id);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);

        for (int c = 0; c < kCascadeCount; ++c) {
            // Attach this cascade's layer.
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      gShadowMap, 0, c);
            glClear(GL_DEPTH_BUFFER_BIT);

            for (auto& entity : gScene.entities) {
                if (entity.light || entity.subMeshes.empty()) continue;
                if (entity.shading != ShadingMode::Toon) continue;

                glm::mat4 model = TransformMatrix(entity.transform);
                glm::mat4 lightMVP = gCascadeMatrices[c] * model;
                glUniformMatrix4fv(glGetUniformLocation(gShadowShader.id, "uLightMVP"),
                    1, GL_FALSE, glm::value_ptr(lightMVP));

                bool doSkin = entity.skinned && !gSettings.debugDisableSkinning;
                glUniform1i(glGetUniformLocation(gShadowShader.id, "uSkinned"),
                    doSkin ? 1 : 0);
                if (doSkin && !entity.animator.jointMatrices.empty()) {
                    int count = std::min(static_cast<int>(entity.animator.jointMatrices.size()),
                                         kMaxJoints);
                    glUniformMatrix4fv(glGetUniformLocation(gShadowShader.id, "uJoints"),
                        count, GL_FALSE,
                        glm::value_ptr(entity.animator.jointMatrices[0]));
                }

                for (auto& sm : entity.subMeshes)
                    DrawMesh(sm.mesh);
            }
        }

        glCullFace(GL_BACK);
        glDisable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, gFramebufferW, gFramebufferH);
    }

    void Render(double /*alpha*/) {
        float aspect = (gFramebufferH > 0)
            ? static_cast<float>(gFramebufferW) / static_cast<float>(gFramebufferH)
            : 1.0f;

        // Shadow pass (before scene).
        if (gSettings.shadowEnabled && gShadowShader.id && gShadowFBO)
            RenderShadowPass(aspect);

        bool postProcess = gSettings.edgeEnabled && gEdgeShader.id && gFBO;

        if (postProcess)
            glBindFramebuffer(GL_FRAMEBUFFER, gFBO);

        glClearColor(gSettings.clearColor.r, gSettings.clearColor.g,
                     gSettings.clearColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 vp = CameraProjectionMatrix(gCamera, aspect)
            * CameraViewMatrix(gCamera);

        // Grid + sky background pass (writes color + depth, scene draws on top).
        if (gSettings.gridEnabled && gGridShader.id) {
            glUseProgram(gGridShader.id);
            glUniformMatrix4fv(glGetUniformLocation(gGridShader.id, "uInvViewProj"),
                1, GL_FALSE, glm::value_ptr(glm::inverse(vp)));
            glUniform3fv(glGetUniformLocation(gGridShader.id, "uCameraPos"),
                1, glm::value_ptr(gCamera.position));
            glUniform3fv(glGetUniformLocation(gGridShader.id, "uSkyTop"),
                1, glm::value_ptr(gSettings.skyTop));
            glUniform3fv(glGetUniformLocation(gGridShader.id, "uSkyBottom"),
                1, glm::value_ptr(gSettings.skyBottom));
            glUniform3fv(glGetUniformLocation(gGridShader.id, "uGridColor"),
                1, glm::value_ptr(gSettings.gridColor));
            glUniform1f(glGetUniformLocation(gGridShader.id, "uGridScale"),
                gSettings.gridScale);
            glUniform1f(glGetUniformLocation(gGridShader.id, "uGridFade"),
                gSettings.gridFade);
            glUniform1f(glGetUniformLocation(gGridShader.id, "uNear"),
                gCamera.nearPlane);
            glUniform1f(glGetUniformLocation(gGridShader.id, "uFar"),
                gCamera.farPlane);

            glDepthFunc(GL_ALWAYS);  // overwrite clear depth
            glBindVertexArray(gFullscreenVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDepthFunc(GL_LESS);    // restore for scene
        }

        for (auto& entity : gScene.entities) {
            if (entity.light) continue;  // lights have no geometry
            switch (entity.shading) {
            case ShadingMode::VertexColor: RenderVertexColor(entity, vp); break;
            case ShadingMode::Toon:        RenderToon(entity, vp);        break;
            }
        }

        // -- Sobel edge detection post-process --------------------------------
        if (postProcess) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glDisable(GL_DEPTH_TEST);
            glUseProgram(gEdgeShader.id);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gColorTex);
            glUniform1i(glGetUniformLocation(gEdgeShader.id, "uSceneColor"), 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gDepthTex);
            glUniform1i(glGetUniformLocation(gEdgeShader.id, "uSceneDepth"), 1);

            glUniform3fv(glGetUniformLocation(gEdgeShader.id, "uEdgeColor"),
                1, glm::value_ptr(gSettings.edgeColor));
            glUniform1f(glGetUniformLocation(gEdgeShader.id, "uEdgeThreshold"),
                gSettings.edgeThreshold);
            glUniform1f(glGetUniformLocation(gEdgeShader.id, "uEdgeWidth"),
                gSettings.edgeWidth);
            glUniform2f(glGetUniformLocation(gEdgeShader.id, "uTexelSize"),
                1.0f / static_cast<float>(gFramebufferW),
                1.0f / static_cast<float>(gFramebufferH));
            glUniform1f(glGetUniformLocation(gEdgeShader.id, "uNear"),
                gCamera.nearPlane);
            glUniform1f(glGetUniformLocation(gEdgeShader.id, "uFar"),
                gCamera.farPlane);

            glBindVertexArray(gFullscreenVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glActiveTexture(GL_TEXTURE0);
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

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Load GL function pointers via GLAD.
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "gladLoadGLLoader failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::printf("GL vendor:   %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    std::printf("GL renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::printf("GL version:  %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::printf("GLSL:        %s\n", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    // GL debug output — core in 4.3+, available as KHR_debug extension on
    // some 4.1 drivers. Not present on macOS; the null check skips it there.
    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GlDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }

    // Register input callbacks.
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);

    // Sync framebuffer dimensions (may differ from window size on HiDPI).
    {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        gFramebufferW = fbw;
        gFramebufferH = fbh;
        glViewport(0, 0, fbw, fbh);
    }

    glEnable(GL_DEPTH_TEST);

    OverlayInit(window);

    // -----------------------------------------------------------------------
    // Resource loading.
    // -----------------------------------------------------------------------
    if (!LoadShader(gShader, "assets/shaders/triangle.vert",
        "assets/shaders/triangle.frag")) {
        std::fprintf(stderr, "Failed to load triangle shaders\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    gDefaultTexture = CreateWhiteTexture();

    // Demo triangle vertex data: position (vec3) + color (vec3) + texcoord (vec2).
    // clang-format off
    float triVerts[] = {
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.5f, 1.0f,
    };
    VertexAttrib triAttribs[] = {
        {3, GL_FLOAT, 0},                  // location 0: position
        {3, GL_FLOAT, 3 * sizeof(float)},  // location 1: color
        {2, GL_FLOAT, 6 * sizeof(float)},  // location 2: texcoord
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
        e.transform = dt.transform;
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
            {3, GL_FLOAT, 0},
            {3, GL_FLOAT, 3  * sizeof(float)},
            {2, GL_FLOAT, 6  * sizeof(float)},
            {3, GL_FLOAT, 8  * sizeof(float), /*location=*/5},
        };
        // clang-format on

        Entity e;
        e.name    = "Test Quad";
        e.shading = ShadingMode::Toon;
        e.transform.position = {0.0f, 0.0f, -1.0f};

        SubMesh sm;
        sm.mesh = CreateMesh(qv, sizeof(qv), 11 * sizeof(float), qAttribs, 4, 6);
        sm.material.texture   = CreateCheckerTexture();
        sm.material.normalMap = CreateTestNormalMap();
        e.subMeshes.push_back(std::move(sm));
        gScene.entities.push_back(std::move(e));
    }

    // Toon + outline shaders. model.vert is reused for the toon pass since
    // it already transforms normals to world space.
    if (!LoadShader(gToonShader, "assets/shaders/model.vert",
        "assets/shaders/toon.frag")) {
        std::fprintf(stderr, "Failed to load toon shaders\n");
    }
    if (!LoadShader(gOutlineShader, "assets/shaders/outline.vert",
        "assets/shaders/outline.frag")) {
        std::fprintf(stderr, "Failed to load outline shaders\n");
    }
    if (!LoadShader(gEdgeShader, "assets/shaders/edge.vert",
        "assets/shaders/edge.frag")) {
        std::fprintf(stderr, "Failed to load edge shaders\n");
    }
    if (!LoadShader(gShadowShader, "assets/shaders/shadow.vert",
        "assets/shaders/shadow.frag")) {
        std::fprintf(stderr, "Failed to load shadow shaders\n");
    }
    if (!LoadShader(gGridShader, "assets/shaders/edge.vert",
        "assets/shaders/grid.frag")) {
        std::fprintf(stderr, "Failed to load grid shaders\n");
    }

    CreateShadowFBO();

    // Fullscreen triangle VAO (attributeless — positions from gl_VertexID).
    glGenVertexArrays(1, &gFullscreenVAO);

    // Off-screen FBO for the edge detection post-process.
    CreateOrResizeFBO(gFramebufferW, gFramebufferH);

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
            e.transform.scale    = glm::vec3(s);
            e.transform.position = -center * s;
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
        const auto   now = Clock::now();
        const double frame = std::chrono::duration<double>(now - prev).count();
        prev = now;

        // Clamp to avoid spiral-of-death after hitches / breakpoints.
        accumulator += (frame > 0.25 ? 0.25 : frame);

        while (accumulator >= kFixedTimestep) {
            Update(kFixedTimestep);
            accumulator -= kFixedTimestep;
        }

        // Camera input runs at frame rate (not fixed timestep) for responsiveness.
        // Skip when ImGui is capturing input so widgets work without fighting
        // the camera.
        if (gRightMouseHeld && !gOverlayCapturing)
            CameraProcessKeyboard(gCamera, window, static_cast<float>(frame));

        ReloadIfChanged(gShader);
        ReloadIfChanged(gToonShader);
        ReloadIfChanged(gOutlineShader);
        ReloadIfChanged(gEdgeShader);
        ReloadIfChanged(gShadowShader);
        ReloadIfChanged(gGridShader);

        const double alpha = accumulator / kFixedTimestep;
        Render(alpha);

        // ImGui overlay (rendered after the scene, on top).
        OverlayNewFrame();
        float fps = (frame > 0.0) ? static_cast<float>(1.0 / frame) : 0.0f;
        gOverlayCapturing = OverlayRender(gSettings, gScene, gCamera, gDefaultTexture, fps);

        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    // -----------------------------------------------------------------------
    // Cleanup — reverse order of creation.
    // -----------------------------------------------------------------------
    OverlayShutdown();
    DestroyScene(gScene);
    if (gFBO)           glDeleteFramebuffers(1, &gFBO);
    if (gColorTex)      glDeleteTextures(1, &gColorTex);
    if (gDepthTex)      glDeleteTextures(1, &gDepthTex);
    if (gFullscreenVAO) glDeleteVertexArrays(1, &gFullscreenVAO);
    if (gShadowFBO) glDeleteFramebuffers(1, &gShadowFBO);
    if (gShadowMap) glDeleteTextures(1, &gShadowMap);
    DestroyShader(gGridShader);
    DestroyShader(gShadowShader);
    DestroyShader(gEdgeShader);
    DestroyShader(gOutlineShader);
    DestroyShader(gToonShader);
    DestroyTexture(gDefaultTexture);
    DestroyShader(gShader);

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
