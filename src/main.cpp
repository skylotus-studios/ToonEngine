//============================================================================
//  ToonEngine — entry point.
//
//  Owns the window + game loop and drives the renderer. All GPU/backend work
//  lives behind core/renderer.h (see that header for the seam rationale); this
//  file includes no Diligent header at all — that's the point of the seam.
//============================================================================
#include "core/renderer.h"
#include "core/primitives.h"
#include "core/scene.h"

#include <cstdint>
#include <cstdio>
#include <vector>

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
// A spinning entity: which scene entity, and the axis its local rotation animates around
// (each frame, rotationEuler = axis * angle).
struct Spinner {
    int        entity;
    toon::Vec3 axis;
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

    // --- Scene graph ---------------------------------------------------------
    // A real entity tree (core/scene.h) instead of a hardcoded array. Root at index 0;
    // everything is a child of the root EXCEPT the satellite, which is parented to the cube
    // to demonstrate hierarchy composition (it orbits the cube as the cube spins).
    toon::Scene scene;
    toon::EnsureSceneRoot(scene);
    std::vector<Spinner> spinners;   // entities whose local rotation animates each frame

    // Ground plane beneath the objects (catches their SSAO contact shadows; no spin/outline).
    {
        toon::Entity& e = scene.entities[toon::AddEntity(scene, 0, "Ground")];
        e.mesh = Upload(renderer, toon::MakePlane(5.0f), "ground");
        e.transform->position   = { 0.0f, -1.05f, 0.0f };
        e.material.baseColor     = { 0.60f, 0.60f, 0.63f };
        e.material.outlineWidth  = 0.0f;
        e.material.roughness     = 0.05f;   // smooth -> reflective (SSR)
    }
    // Sphere — non-uniformly scaled into a spinning ellipsoid (exercises the normal matrix).
    {
        const int i = toon::AddEntity(scene, 0, "Sphere");
        toon::Entity& e = scene.entities[i];
        e.mesh = Upload(renderer, toon::MakeUVSphere(1.0f, 32, 48), "sphere");
        e.transform->position = { -2.8f, 0.0f, 0.0f };
        e.transform->scale    = {  1.5f, 0.8f, 1.0f };
        e.material = toon::Material{ {0.85f, 0.30f, 0.35f}, {0.24f, 0.05f, 0.08f}, 0.030f };
        e.material.roughness = 0.15f;   // lightly glossy so SSR reflects on it
        spinners.push_back({ i, { 0.0f, 1.0f, 0.0f } });
    }
    // Cube — the satellite's parent.
    const int cubeIdx = toon::AddEntity(scene, 0, "Cube");
    {
        toon::Entity& e = scene.entities[cubeIdx];
        e.mesh = Upload(renderer, toon::MakeCube(0.9f), "cube");
        e.material = toon::Material{ {0.30f, 0.45f, 0.85f}, {0.02f, 0.02f, 0.05f}, 0.050f };
        e.material.roughness = 0.15f;
        spinners.push_back({ cubeIdx, { 0.5f, 1.0f, 0.0f } });
    }
    // Torus.
    {
        const int i = toon::AddEntity(scene, 0, "Torus");
        toon::Entity& e = scene.entities[i];
        e.mesh = Upload(renderer, toon::MakeTorus(0.75f, 0.32f, 48, 24), "torus");
        e.transform->position = { 2.8f, 0.0f, 0.0f };
        e.material = toon::Material{ {0.90f, 0.70f, 0.25f}, {0.32f, 0.20f, 0.03f}, 0.022f };
        e.material.roughness = 0.15f;
        spinners.push_back({ i, { 1.0f, 0.0f, 0.0f } });
    }
    // Loaded glTF model (DiligentTools' loader): cel-shaded albedo + inverted-hull outline.
    const toon::ModelHandle helmet = renderer.LoadModel(TOON_MODELS_DIR "/helmet.glb");
    if (helmet != toon::ModelHandle::Invalid) {
        const int i = toon::AddEntity(scene, 0, "Helmet");
        toon::Entity& e = scene.entities[i];
        e.model = helmet;
        e.transform->position = { 0.0f, 2.5f, 0.0f };
        e.transform->scale    = { 1.4f, 1.4f, 1.4f };
        e.material.baseColor    = { 1.0f, 1.0f, 1.0f };    // white tint (glTF supplies the color)
        e.material.outlineColor = { 0.02f, 0.02f, 0.03f };
        e.material.outlineWidth = 0.04f;
        e.material.roughness    = 0.5f;
        spinners.push_back({ i, { 0.0f, 1.0f, 0.0f } });
    }
    // Satellite — a small sphere PARENTED to the cube (the hierarchy demo). It has no spin of
    // its own; it orbits the cube purely by inheriting the cube's spinning world transform.
    {
        toon::Entity& e = scene.entities[toon::AddEntity(scene, cubeIdx, "Satellite")];
        e.mesh = Upload(renderer, toon::MakeUVSphere(0.22f, 16, 24), "satellite");
        e.transform->position = { 1.7f, 0.0f, 0.0f };   // offset from the cube (its parent)
        e.material = toon::Material{ {0.40f, 0.90f, 0.55f}, {0.03f, 0.07f, 0.04f}, 0.014f };
        e.material.roughness = 0.15f;
    }

    toon::Camera camera;
    camera.distance = 10.0f;
    camera.pitch    = 0.25f;      // look down a little so the ground + its AO show

    toon::Vec3 lightDir{ 0.5f, 0.8f, -0.3f };

    // Style shared by every object each frame: band count + ambient floor (a global
    // shading look). Outline color/width are per-object (above), but this scales all of
    // their widths together — handy for dialing the whole scene's line weight at once.
    toon::Material style;
    float outlineScale = 1.0f;   // global multiplier over each object's outline width

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
        if (spin) spinAngle += dt * 0.6f;

        // Animate the spinning entities' local rotation, then compose the hierarchy's world
        // matrices (parents before children). Motion vectors come from the cached previous
        // world matrices, so no prev-angle bookkeeping is needed here.
        for (const Spinner& s : spinners)
            if (scene.entities[s.entity].transform)
                scene.entities[s.entity].transform->rotationEuler = s.axis * spinAngle;
        toon::UpdateWorldTransforms(scene);

        renderer.BeginFrame(clearColor);

        // Post params up front: SetCamera reads them to decide the TAA jitter.
        renderer.SetPostParams(post);

        // Scene first, so the debug UI overlays it.
        renderer.SetCamera(camera);
        renderer.SetLight(lightDir);

        // Walk the scene, drawing every renderable entity with its hierarchy-composed world
        // matrix (+ last frame's, for motion vectors). The shared style overlays band count,
        // ambient, and the global outline-width multiplier onto each entity's own material.
        for (const toon::Entity& e : scene.entities) {
            const bool isMesh  = e.mesh  != toon::MeshHandle::Invalid;
            const bool isModel = e.model != toon::ModelHandle::Invalid;
            if (!isMesh && !isModel) continue;   // root / non-renderable

            toon::Material m = e.material;
            m.bands        = style.bands;
            m.ambient      = style.ambient;
            m.outlineWidth = e.material.outlineWidth * outlineScale;
            if (isMesh) renderer.DrawMesh(e.mesh,   e.worldMatrix, e.prevWorldMatrix, m);
            else        renderer.DrawModel(e.model, e.worldMatrix, e.prevWorldMatrix, m);
        }

        // Resolve the HDR scene to the back buffer (post effects + exposure + tone map).
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
            ImGui::SliderFloat("Outline width x", &outlineScale, 0.0f, 3.0f);

            // Per-entity material: base color + outline (color + width). Walks the scene's
            // renderable entities (skips the root / non-renderables).
            ImGui::SeparatorText("Objects");
            for (int i = 0; i < static_cast<int>(scene.entities.size()); ++i) {
                toon::Entity& e = scene.entities[i];
                if (e.mesh == toon::MeshHandle::Invalid && e.model == toon::ModelHandle::Invalid)
                    continue;
                ImGui::PushID(i);
                ImGui::Text("%s", e.name.c_str());
                ImGui::ColorEdit3("Base color",     &e.material.baseColor.x);
                ImGui::SliderFloat("Outline width", &e.material.outlineWidth, 0.0f, 0.15f);
                ImGui::ColorEdit3("Outline color",  &e.material.outlineColor.x);
                ImGui::PopID();
            }

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

            ImGui::Checkbox("Depth of field", &post.dof);
            if (post.dof) {
                ImGui::SliderFloat("Focus distance", &post.dofFocusDist, 3.0f, 25.0f);
                ImGui::SliderFloat("Aperture (f-stop)", &post.dofFStop, 1.0f, 16.0f);
                ImGui::SliderFloat("Max blur (CoC)", &post.dofMaxCoC, 0.0f, 0.05f, "%.3f");
            }

            ImGui::Checkbox("TAA (softens toon edges)", &post.taa);

            ImGui::Checkbox("SSR (reflections in the ground)", &post.ssr);
            if (post.ssr)
                ImGui::SliderFloat("Reflection strength", &post.ssrStrength, 0.0f, 1.5f);
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