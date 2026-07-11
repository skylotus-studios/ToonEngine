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
#include "core/camera.h"
#include "core/input.h"

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

// A clean dark editor theme (trimmed from ToonEngineOld/src/ui/themes.cpp): ImGui's dark
// base + softer rounding/padding and a muted-blue accent on the interactive bits. Purely a
// style-struct edit — no backend state — so it's safe to call once after InitUI.
void ApplyToonTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 4.0f;
    s.GrabRounding      = 4.0f;
    s.TabRounding       = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.WindowPadding     = ImVec2(10.0f, 10.0f);
    s.FramePadding      = ImVec2(8.0f, 4.0f);
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.IndentSpacing     = 16.0f;
    s.WindowBorderSize  = 1.0f;

    const ImVec4 accent    = ImVec4(0.26f, 0.45f, 0.78f, 1.00f);
    const ImVec4 accentDim = ImVec4(0.26f, 0.45f, 0.78f, 0.55f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.11f, 0.115f, 0.13f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.24f, 0.27f, 0.33f, 1.00f);
    c[ImGuiCol_Header]           = accentDim;
    c[ImGuiCol_HeaderHovered]    = accent;
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_Button]           = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = accent;
    c[ImGuiCol_ButtonActive]     = ImVec4(0.36f, 0.55f, 0.88f, 1.00f);
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.36f, 0.55f, 0.88f, 1.00f);
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.16f, 0.19f, 0.26f, 1.00f);
    c[ImGuiCol_Tab]              = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    c[ImGuiCol_TabHovered]       = accent;
    c[ImGuiCol_TabSelected]      = ImVec4(0.22f, 0.30f, 0.44f, 1.00f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_TextSelectedBg]   = accentDim;
}
} // namespace

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // We render with Vulkan, so tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Start maximized so the editor fills the screen and the right-docked panels (Inspector /
    // Debug) stay on-screen on any monitor. Creating oversize (e.g. 3840x2160 on a smaller
    // display) pushed the dock layout's right column off the visible area. The 1600x900 below
    // is just the restored-down size.
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "ToonEngine", nullptr, nullptr);
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

    // Install input callbacks (scroll) BEFORE InitUI, so ImGui's GLFW backend chains ours
    // instead of overwriting them.
    toon::Input::Init(window);

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

    // Editor look: apply the dark theme once, now the UI backend is up.
    ApplyToonTheme();

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
    // Satellite — a small sphere PARENTED to the cube (the hierarchy demo). It has no spin of
    // its own; it orbits the cube purely by inheriting the cube's spinning world transform.
    // Created right after the cube so the flat outliner (vector order) lists it directly under
    // its parent — keeping the scripted scene in pre-order, as the editor mutations always are.
    {
        toon::Entity& e = scene.entities[toon::AddEntity(scene, cubeIdx, "Satellite")];
        e.mesh = Upload(renderer, toon::MakeUVSphere(0.22f, 16, 24), "satellite");
        e.transform->position = { 1.7f, 0.0f, 0.0f };   // offset from the cube (its parent)
        e.material = toon::Material{ {0.40f, 0.90f, 0.55f}, {0.03f, 0.07f, 0.04f}, 0.014f };
        e.material.roughness = 0.15f;
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

    // Start with the cube selected so the Inspector is populated on launch.
    scene.selected = cubeIdx;

    // Editor camera — driven by the mouse/keyboard in the loop (defaults: pivot at the
    // origin, distance 10, a slight downward pitch so the ground + its AO show).
    toon::Camera camera;
    const toon::Camera cameraDefault = camera;   // for the "Reset camera" button

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

        // Editor camera: poll input, gate on ImGui's capture (last frame's UI state), then
        // navigate. Right-drag orbits (+ WASD/QE = fly); middle-drag pans; scroll zooms;
        // F focuses the origin. Dragging over the debug panel is suppressed by the gate.
        toon::Input::BeginFrame();
        const ImGuiIO& io = ImGui::GetIO();
        toon::Input::SetCaptured(io.WantCaptureMouse, io.WantCaptureKeyboard);
        {
            using M = toon::Input::Mouse;
            using K = toon::Input::Key;
            float mdx = 0.0f, mdy = 0.0f;
            toon::Input::MouseDelta(mdx, mdy);
            if (toon::Input::IsMouseDown(M::Right)) {
                toon::CameraOrbit(camera, -mdx, -mdy);
                const float fwd = (toon::Input::IsKeyDown(K::W) ? 1.0f : 0.0f) - (toon::Input::IsKeyDown(K::S) ? 1.0f : 0.0f);
                const float rgt = (toon::Input::IsKeyDown(K::D) ? 1.0f : 0.0f) - (toon::Input::IsKeyDown(K::A) ? 1.0f : 0.0f);
                const float upv = (toon::Input::IsKeyDown(K::E) ? 1.0f : 0.0f) - (toon::Input::IsKeyDown(K::Q) ? 1.0f : 0.0f);
                toon::CameraFly(camera, dt, fwd, rgt, upv);
            }
            if (toon::Input::IsMouseDown(M::Middle)) toon::CameraPan(camera, mdx, mdy);
            if (const float s = toon::Input::ScrollDelta(); s != 0.0f) toon::CameraZoom(camera, s);
            if (toon::Input::IsKeyDown(K::F))          toon::CameraFocus(camera, { 0.0f, 0.0f, 0.0f });
        }

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
            // Hierarchy on the far left; Inspector (top) + Debug (bottom) stacked on the
            // right; the 3D scene shows through the pass-through center.
            ImGuiID centerId = dockspaceId;
            const ImGuiID leftId     = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left,  0.20f, nullptr, &centerId);
            ImGuiID       rightId    = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.34f, nullptr, &centerId);
            const ImGuiID rightTopId = ImGui::DockBuilderSplitNode(rightId,  ImGuiDir_Up,    0.55f, nullptr, &rightId);
            ImGui::DockBuilderDockWindow("Scene Hierarchy", leftId);
            ImGui::DockBuilderDockWindow("Inspector",       rightTopId);
            ImGui::DockBuilderDockWindow("ToonEngine Debug", rightId);
            ImGui::DockBuilderFinish(dockspaceId);
        }
#endif

        // --- Scene Hierarchy: select / add / duplicate / delete / drag-drop reparent -----
        // A flat list over scene.entities (parents always precede children), indented by
        // depth so it reads as a tree. Structural edits reorder the vector and invalidate
        // indices, so the loop only RECORDS a pending op / drop and applies them afterward.
        enum class HierOp { None, AddChild, Duplicate, Delete };
        HierOp pendingOp     = HierOp::None;
        int    pendingTarget = -1;
        enum class DropKind { Child, Before, After };
        int      dropSrc  = -1, dropDst = -1;
        DropKind dropKind = DropKind::Child;

        if (ImGui::Begin("Scene Hierarchy")) {
            const int n = static_cast<int>(scene.entities.size());
            for (int i = 0; i < n; ++i) {
                const toon::Entity& e = scene.entities[i];
                const bool isRoot = (e.parent == -1);

                // Depth = length of the parent chain (drives the indent).
                int depth = 0;
                for (int p = e.parent, guard = 0; p >= 0 && p < n && guard < n;
                     p = scene.entities[p].parent, ++guard)
                    ++depth;

                ImGui::PushID(i);
                if (depth > 0) ImGui::Indent(depth * 16.0f);

                const bool selected = (scene.selected == i);
                if (ImGui::Selectable(e.name.c_str(), selected, ImGuiSelectableFlags_SpanAvailWidth))
                    scene.selected = selected ? -1 : i;   // click toggles selection off

                // Drag source (everything but the root).
                if (!isRoot && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("TOON_ENTITY_IDX", &i, sizeof(int));
                    ImGui::Text("%s", e.name.c_str());
                    ImGui::EndDragDropSource();
                }
                // Drop target: cursor-Y within the row picks the zone — top/bottom quarter =
                // sibling before/after, middle = make-child (the root only accepts children).
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TOON_ENTITY_IDX")) {
                        const ImVec2 rmin = ImGui::GetItemRectMin();
                        const ImVec2 rmax = ImGui::GetItemRectMax();
                        const float  frac = (ImGui::GetIO().MousePos.y - rmin.y) / (rmax.y - rmin.y);
                        dropSrc = *static_cast<const int*>(pl->Data);
                        dropDst = i;
                        if      (isRoot)       dropKind = DropKind::Child;
                        else if (frac < 0.25f) dropKind = DropKind::Before;
                        else if (frac > 0.75f) dropKind = DropKind::After;
                        else                   dropKind = DropKind::Child;
                    }
                    ImGui::EndDragDropTarget();
                }
                // Right-click: structural ops (Duplicate/Delete disabled on the root).
                if (ImGui::BeginPopupContextItem()) {
                    scene.selected = i;
                    if (ImGui::MenuItem("Add Child"))                          { pendingOp = HierOp::AddChild;  pendingTarget = i; }
                    if (ImGui::MenuItem("Duplicate", nullptr, false, !isRoot)) { pendingOp = HierOp::Duplicate; pendingTarget = i; }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", nullptr, false, !isRoot))    { pendingOp = HierOp::Delete;    pendingTarget = i; }
                    ImGui::EndPopup();
                }

                if (depth > 0) ImGui::Unindent(depth * 16.0f);
                ImGui::PopID();
            }
        }
        ImGui::End();

        // Apply the one recorded structural op, then the drag-drop — indices are stable now.
        switch (pendingOp) {
            case HierOp::AddChild:  scene.selected = toon::AddChildEntity(scene, pendingTarget, "Entity"); break;
            case HierOp::Duplicate: { const int d = toon::DuplicateEntity(scene, pendingTarget); if (d >= 0) scene.selected = d; } break;
            case HierOp::Delete:    toon::DeleteEntity(scene, pendingTarget); break;
            case HierOp::None:      break;
        }
        if (dropSrc >= 0 && dropDst >= 0) {
            if (dropKind == DropKind::Child) toon::ReparentEntity(scene, dropSrc, dropDst);
            else toon::MoveEntityAsSibling(scene, dropSrc, dropDst, dropKind == DropKind::Before);
        }

        // --- Inspector: edit the selected entity (name / transform / material) -----------
        if (ImGui::Begin("Inspector")) {
            if (scene.selected < 0 || scene.selected >= static_cast<int>(scene.entities.size())) {
                ImGui::TextDisabled("Select an entity in the hierarchy.");
            } else {
                toon::Entity& e = scene.entities[scene.selected];
                const bool isRoot = (e.parent == -1);

                char nameBuf[128];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                    e.name = nameBuf;

                // Transform — rotation shown in DEGREES for editing, stored in radians.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Transform");
                    toon::Transform& t = *e.transform;
                    constexpr float kRad2Deg = 57.29578f, kDeg2Rad = 0.01745329f;
                    ImGui::DragFloat3("Position", &t.position.x, 0.01f);
                    float deg[3] = { t.rotationEuler.x * kRad2Deg,
                                     t.rotationEuler.y * kRad2Deg,
                                     t.rotationEuler.z * kRad2Deg };
                    if (ImGui::DragFloat3("Rotation", deg, 0.5f))
                        t.rotationEuler = { deg[0] * kDeg2Rad, deg[1] * kDeg2Rad, deg[2] * kDeg2Rad };
                    ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
                } else if (isRoot) {
                    ImGui::TextDisabled("(scene root — a pure anchor, no transform)");
                }

                // Material — only for renderables (a mesh or a model).
                if (e.mesh != toon::MeshHandle::Invalid || e.model != toon::ModelHandle::Invalid) {
                    ImGui::SeparatorText("Material");
                    ImGui::ColorEdit3("Base color",    &e.material.baseColor.x);
                    ImGui::ColorEdit3("Outline color", &e.material.outlineColor.x);
                    ImGui::DragFloat("Outline width", &e.material.outlineWidth, 0.001f, 0.0f, 0.5f, "%.3f");
                    ImGui::SliderFloat("Roughness", &e.material.roughness, 0.0f, 1.0f);
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("ToonEngine Debug")) {
            ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate,
                        1000.0f / ImGui::GetIO().Framerate);

            ImGui::SeparatorText("Light");
            ImGui::SliderFloat3("Direction", &lightDir.x, -1.0f, 1.0f);

            ImGui::SeparatorText("Style (all objects)");
            ImGui::SliderFloat("Bands", &style.bands, 1.0f, 8.0f, "%.0f");
            ImGui::SliderFloat("Ambient", &style.ambient, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline width x", &outlineScale, 0.0f, 3.0f);
            ImGui::TextDisabled("Per-object color/outline: see the Inspector.");

            ImGui::SeparatorText("Camera");
            ImGui::TextDisabled("Right-drag: orbit (+WASD/QE fly)");
            ImGui::TextDisabled("Mid-drag: pan | Scroll: zoom | F: focus");
            ImGui::SliderAngle("FOV", &camera.fovY, 20.0f, 100.0f);
            if (ImGui::Button("Reset camera")) camera = cameraDefault;
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