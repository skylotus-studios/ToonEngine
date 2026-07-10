//============================================================================
//  ToonEngine — entry point.
//
//  Owns the window + game loop and drives the renderer. All GPU/backend work
//  lives behind core/renderer.h (see that header for the seam rationale); this
//  file includes no Diligent header at all — that's the point of the seam.
//============================================================================
#include "core/renderer.h"
#include "core/primitives.h"

#include <array>
#include <cstdint>
#include <cstdio>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Dear ImGui is a plain UI library, not a Diligent type, so engine/game code
// is free to include it directly and call ImGui:: between Renderer::BeginUI()
// and EndUI(). Diligent's ImGui *renderer* glue stays behind the seam.
#include "imgui.h"
#ifdef IMGUI_HAS_DOCK
#include "imgui_internal.h"   // DockBuilder API, for the one-time default layout
#endif

namespace {
// A scene object: which mesh, how it looks, where it sits, and its spin axis.
struct Object {
    toon::MeshHandle mesh;
    toon::Material   material;
    toon::Vec3       position;
    toon::Vec3       spinAxis;   // rotationEuler = spinAxis * angle
};

// Upload a CPU mesh and return its handle (logs on failure).
toon::MeshHandle Upload(toon::Renderer& r, const toon::MeshData& m, const char* name) {
    const toon::MeshHandle h = r.CreateMesh(
        m.vertices.data(), static_cast<uint32_t>(m.vertices.size()),
        m.indices.data(),  static_cast<uint32_t>(m.indices.size()));
    if (h == toon::MeshHandle::Invalid) std::fprintf(stderr, "Failed to create mesh '%s'\n", name);
    return h;
}
} // namespace

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // We render with Vulkan, so tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(3840, 2160, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    toon::Renderer renderer;
    if (!renderer.Init(window)) {
        std::fprintf(stderr, "Renderer init failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!renderer.InitUI(window)) {
        std::fprintf(stderr, "Renderer UI init failed\n");
        renderer.Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Editor-style docking: panels can be dragged to snap around the 3D view.
    // (ImGui is exempt from the renderer seam, so app code drives UI policy.)
    // Guarded on IMGUI_HAS_DOCK so the build stays green with a non-docking imgui
    // — docking activates only when a docking-branch imgui is checked out.
#ifdef IMGUI_HAS_DOCK
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

    // Route framebuffer resizes to the renderer's swap chain.
    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        if (auto* r = static_cast<toon::Renderer*>(glfwGetWindowUserPointer(w)))
            r->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        });

    // --- Scene ---------------------------------------------------------------
    // Three primitives in a row: a smooth sphere, a faceted cube (per-face
    // shading, smooth-normal outline hull), and a torus. Each has its own color.
    std::array<Object, 3> objects{ {
        { Upload(renderer, toon::MakeUVSphere(1.0f, 32, 48),          "sphere"),
          toon::Material{ {0.85f, 0.30f, 0.35f} }, {-2.8f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
        { Upload(renderer, toon::MakeCube(0.9f),                      "cube"),
          toon::Material{ {0.30f, 0.45f, 0.85f} }, { 0.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f} },
        { Upload(renderer, toon::MakeTorus(0.75f, 0.32f, 48, 24),     "torus"),
          toon::Material{ {0.90f, 0.70f, 0.25f} }, { 2.8f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
    } };

    // Ground plane beneath the trio, so SSAO has a surface to catch their contact
    // shadows (drawn separately below — it doesn't spin and wants no outline).
    const toon::MeshHandle groundMesh = Upload(renderer, toon::MakePlane(5.0f), "ground");
    toon::Material groundMaterial{ {0.60f, 0.60f, 0.63f} };
    groundMaterial.outlineWidth = 0.0f;

    toon::Camera camera;
    camera.distance = 10.0f;
    camera.pitch    = 0.22f;      // look down a little so the ground (and its AO) shows

    toon::Vec3 lightDir{ 0.5f, 0.8f, -0.3f };

    // Shared style applied to every object each frame (band count, outline, …).
    toon::Material style;

    // HDR post-processing (foundation for DiligentFX effects).
    toon::PostParams post;

    bool  spin = true;
    float spinAngle = 0.0f;
#ifdef IMGUI_HAS_DOCK
    bool  dockLayoutBuilt = false;
#endif
    double lastTime = glfwGetTime();

    const toon::Color clearColor{ 0.10f, 0.11f, 0.13f, 1.0f };
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - lastTime);
        lastTime = now;
        const float prevSpinAngle = spinAngle;      // last frame's angle (for motion vectors)
        if (spin) spinAngle += dt * 0.6f;

        renderer.BeginFrame(clearColor);

        // Scene first, so the debug UI overlays it.
        renderer.SetCamera(camera);
        renderer.SetLight(lightDir);

        // Ground plane, just below the objects (fixed; catches their AO contact
        // shadows). It never moves, so its previous transform equals its current one.
        {
            groundMaterial.bands   = style.bands;
            groundMaterial.ambient = style.ambient;
            toon::Transform groundXform;
            groundXform.position = { 0.0f, -1.05f, 0.0f };   // just under the sphere/torus
            renderer.DrawMesh(groundMesh, groundXform, groundXform, groundMaterial);
        }

        for (Object& obj : objects) {
            // Push the shared style onto each object's material (keep its color).
            obj.material.outlineColor = style.outlineColor;
            obj.material.outlineWidth = style.outlineWidth;
            obj.material.bands        = style.bands;
            obj.material.ambient      = style.ambient;

            // Current + previous placement — the delta is this object's motion vector.
            toon::Transform xform;
            xform.position      = obj.position;
            xform.rotationEuler = obj.spinAxis * spinAngle;
            toon::Transform prevXform = xform;
            prevXform.rotationEuler = obj.spinAxis * prevSpinAngle;
            renderer.DrawMesh(obj.mesh, xform, prevXform, obj.material);
        }

        // Resolve the HDR scene to the back buffer (exposure + tone map).
        renderer.SetPostParams(post);
        renderer.EndScene();

        renderer.BeginUI();

#ifdef IMGUI_HAS_DOCK
        // Full-window dock space with a see-through center so the scene shows
        // through; panels dock around it. Build the default layout (debug panel
        // docked left) once — after that, whatever the user arranges sticks.
        const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
            0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (!dockLayoutBuilt) {
            dockLayoutBuilt = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
            ImGuiID centerId = dockspaceId;
            const ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.28f, nullptr, &centerId);
            ImGui::DockBuilderDockWindow("ToonEngine Debug", leftId);
            ImGui::DockBuilderFinish(dockspaceId);
        }
#endif

        if (ImGui::Begin("ToonEngine Debug")) {
            ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate,
                        1000.0f / ImGui::GetIO().Framerate);

            ImGui::SeparatorText("Light");
            ImGui::SliderFloat3("Direction", &lightDir.x, -1.0f, 1.0f);

            ImGui::SeparatorText("Style (all objects)");
            ImGui::SliderFloat("Bands", &style.bands, 1.0f, 8.0f, "%.0f");
            ImGui::SliderFloat("Ambient", &style.ambient, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline width", &style.outlineWidth, 0.0f, 0.15f);
            ImGui::ColorEdit3("Outline color", &style.outlineColor.x);

            ImGui::SeparatorText("Base colors");
            ImGui::ColorEdit3("Sphere", &objects[0].material.baseColor.x);
            ImGui::ColorEdit3("Cube",   &objects[1].material.baseColor.x);
            ImGui::ColorEdit3("Torus",  &objects[2].material.baseColor.x);

            ImGui::SeparatorText("Camera");
            ImGui::SliderFloat("Distance", &camera.distance, 3.0f, 25.0f);
            ImGui::SliderAngle("Yaw", &camera.yaw);
            ImGui::SliderAngle("Pitch", &camera.pitch, -89.0f, 89.0f);
            ImGui::Checkbox("Spin", &spin);

            ImGui::SeparatorText("Post (HDR)");
            ImGui::Checkbox("Tone map (ACES)", &post.toneMap);
            ImGui::SliderFloat("Exposure", &post.exposure, 0.1f, 4.0f);

            ImGui::Checkbox("Bloom", &post.bloom);
            if (post.bloom) {
                ImGui::SliderFloat("Intensity", &post.bloomIntensity, 0.0f, 1.0f);
                ImGui::SliderFloat("Threshold", &post.bloomThreshold, 0.0f, 1.5f);
                ImGui::SliderFloat("Soft knee", &post.bloomSoftKnee, 0.0f, 1.0f);
                ImGui::SliderFloat("Bloom radius", &post.bloomRadius, 0.3f, 0.85f);
            }

            ImGui::Checkbox("SSAO", &post.ssao);
            if (post.ssao) {
                ImGui::SliderFloat("AO strength", &post.ssaoStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("AO radius", &post.ssaoRadius, 0.1f, 3.0f);
                ImGui::Checkbox("AO temporal (motion-vector denoise)", &post.ssaoTemporal);
            }
        }
        ImGui::End();
        renderer.EndUI();

        renderer.EndFrame();
    }

    renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}