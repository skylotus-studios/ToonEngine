// ImGui debug overlay implementation.

#include "overlay.h"
#include "ui/themes.h"

#include "core/animator.h"
#include "core/transform.h"
#include "scene/serializer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

static Theme sCurrentTheme = Theme::AmberYellow;

void OverlayInit(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    io.Fonts->AddFontFromFileTTF("assets/fonts/BaiJamjuree-Medium.ttf", 25.0f);
    ApplyTheme(sCurrentTheme);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
}

void OverlayNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

static const char* ShadingModeLabel(ShadingMode m) {
    switch (m) {
    case ShadingMode::VertexColor: return "Vertex Color";
    case ShadingMode::Toon:        return "Toon";
    }
    return "?";
}

static const char* LightTypeLabel(LightType t) {
    switch (t) {
    case LightType::Directional: return "Directional";
    case LightType::Point:       return "Point";
    }
    return "?";
}

bool OverlayRender(RenderSettings& s, Scene& scene, Camera& camera,
    TextureHandle& defaultTexture, float fps) {
    // -- Main menu bar --------------------------------------------------------
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::BeginMenu("Preferences")) {
                if (ImGui::BeginMenu("Theme")) {
                    for (int i = 0; i < static_cast<int>(Theme::Count); ++i) {
                        Theme t = static_cast<Theme>(i);
                        if (ImGui::MenuItem(ThemeName(t), nullptr, t == sCurrentTheme)) {
                            sCurrentTheme = t;
                            ApplyTheme(t);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Fullscreen DockSpace — invisible background target for snapping panels.
    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

    // -- Render settings panel ------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(16, 36), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("Inspector");

    ImGui::Text("%.1f FPS (%.2f ms)", fps, (fps > 0.0f) ? 1000.0f / fps : 0.0f);
    ImGui::Separator();

    // Gizmo toolbar: W=Translate, E=Rotate, R=Scale.
    {
        bool t = (s.gizmoOp == 0), r = (s.gizmoOp == 1), sc = (s.gizmoOp == 2);
        if (ImGui::RadioButton("Move", t))  s.gizmoOp = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", r))   s.gizmoOp = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", sc))  s.gizmoOp = 2;
        ImGui::SameLine();
        ImGui::Checkbox("Local", &s.gizmoLocal);
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &s.gizmoSnap);
        if (s.gizmoSnap) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::DragFloat("##snap", &s.gizmoSnapValue, 0.1f, 0.1f, 10.0f);
        }
    }
    ImGui::Separator();

    // -- Selected-entity inspection ------------------------------------------
    // Transform / Light are displayed as component-style collapsing headers.
    // Mesh + shading stats sit at the bottom since they're read-only summary.
    {
        int count = static_cast<int>(scene.entities.size());
        if (scene.selected >= 0 && scene.selected < count) {
            Entity& e = scene.entities[scene.selected];

            // Transform component.
            if (e.transform &&
                ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Position", &e.transform->position.x, 0.05f);
                ImGui::DragFloat3("Rotation", &e.transform->rotation.x, 0.5f);
                ImGui::DragFloat3("Scale",    &e.transform->scale.x,    0.01f, 0.01f, 100.0f);
            }

            // Light component.
            if (e.light &&
                ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                Light& L = *e.light;
                if (ImGui::BeginCombo("Type", LightTypeLabel(L.type))) {
                    if (ImGui::Selectable("Directional", L.type == LightType::Directional))
                        L.type = LightType::Directional;
                    if (ImGui::Selectable("Point", L.type == LightType::Point))
                        L.type = LightType::Point;
                    ImGui::EndCombo();
                }
                ImGui::ColorEdit3("Color", &L.color.x);
                ImGui::SliderFloat("Intensity", &L.intensity, 0.0f, 5.0f);
                if (L.type == LightType::Directional) {
                    ImGui::DragFloat3("Direction", &L.direction.x, 0.01f, -1.0f, 1.0f);
                } else {
                    ImGui::SliderFloat("Radius", &L.radius, 0.1f, 50.0f);
                }
            }

            // Animation component (skinned entities with clips).
            if (e.skinned && !e.clips.empty() &&
                ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Joints: %d  |  Clips: %d",
                    static_cast<int>(e.skeleton.joints.size()),
                    static_cast<int>(e.clips.size()));

                const char* currentName = (e.animator.clipIndex >= 0 &&
                    e.animator.clipIndex < static_cast<int>(e.clips.size()))
                    ? e.clips[e.animator.clipIndex].name.c_str() : "(none)";
                if (ImGui::BeginCombo("Clip", currentName)) {
                    for (int ci = 0; ci < static_cast<int>(e.clips.size()); ++ci) {
                        bool sel = (ci == e.animator.clipIndex);
                        if (ImGui::Selectable(e.clips[ci].name.c_str(), sel))
                            AnimatorSetClip(e.animator, e.skeleton, ci);
                    }
                    ImGui::EndCombo();
                }

                if (e.animator.clipIndex >= 0) {
                    float dur = e.clips[e.animator.clipIndex].duration;
                    ImGui::SliderFloat("Time", &e.animator.time, 0.0f, dur, "%.2f s");
                    ImGui::Checkbox("Play", &e.animator.playing);
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop", &e.animator.looping);
                }
            }

            // Read-only mesh stats — kept at the bottom of the inspector.
            if (!e.light && e.parent != -1 &&
                ImGui::CollapsingHeader("Mesh Stats")) {
                int meshCount = 0;
                int triCount  = 0;
                for (auto& sm : e.subMeshes) {
                    ++meshCount;
                    int ic = GetMeshIndexCount(sm.mesh);
                    int n = ic > 0 ? ic : GetMeshVertexCount(sm.mesh);
                    triCount += n / 3;
                }
                ImGui::Text("Shading: %s",  ShadingModeLabel(e.shading));
                ImGui::Text("Meshes: %d",   meshCount);
                ImGui::Text("Triangles: %d", triCount);
            }
        } else {
            ImGui::TextDisabled("Select an entity to inspect");
        }
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Toon Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Band High", &s.bandThresholdHigh, -1.0f, 1.0f);
        ImGui::SliderFloat("Band Low", &s.bandThresholdLow, -1.0f, 1.0f);
        ImGui::SliderFloat("Bright", &s.brightIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Mid", &s.midIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Shadow", &s.shadowIntensity, 0.0f, 1.0f);
        ImGui::ColorEdit3("Shadow Tint", &s.shadowTint.x);
    }

    if (ImGui::CollapsingHeader("Specular", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Spec Color", &s.specColor.x);
        ImGui::SliderFloat("Threshold", &s.specThreshold, 0.5f, 1.0f);
        ImGui::SliderFloat("Strength", &s.specStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("Shininess", &s.specShininess, 2.0f, 128.0f);
    }

    if (ImGui::CollapsingHeader("Rim Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Rim Color", &s.rimColor.x);
        ImGui::SliderFloat("Rim Power", &s.rimPower, 0.5f, 10.0f);
        ImGui::SliderFloat("Rim Strength", &s.rimStrength, 0.0f, 2.0f);
    }

    if (ImGui::CollapsingHeader("Outlines", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Outline Width", &s.outlineWidth, 0.0f, 2.0f);
        ImGui::ColorEdit4("Outline Color", &s.outlineColor.x);
        ImGui::Checkbox("Even Thickness", &s.outlineScreenSpace);
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##shadow", &s.shadowEnabled);
        ImGui::SliderFloat("Bias", &s.shadowBias, 0.0f, 0.01f, "%.4f");
        ImGui::SliderFloat("Distance", &s.shadowDistance, 1.0f, 100.0f);
        ImGui::Text("4 cascades, 2048x2048");
    }

    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Grid", &s.gridEnabled);
        ImGui::ColorEdit3("Sky Top", &s.skyTop.x);
        ImGui::ColorEdit3("Sky Bottom", &s.skyBottom.x);
        if (s.gridEnabled) {
            ImGui::ColorEdit3("Grid Color", &s.gridColor.x);
            ImGui::SliderFloat("Grid Scale", &s.gridScale, 0.1f, 10.0f);
            ImGui::SliderFloat("Grid Fade", &s.gridFade, 1.0f, 100.0f);
        }
        ImGui::ColorEdit3("Fallback BG", &s.clearColor.x);
        ImGui::Separator();
        if (ImGui::Button("Save Scene"))
            SaveScene("scene.scene", scene, camera, s);
        ImGui::SameLine();
        if (ImGui::Button("Load Scene"))
            LoadScene("scene.scene", scene, camera, s, defaultTexture);
    }

    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Disable Skinning", &s.debugDisableSkinning);
    }

    ImGui::End();

    // -- Entity list panel ----------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(16, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 350), ImGuiCond_FirstUseEver);

    ImGui::Begin("Scene Hierarchy");

    auto& entities = scene.entities;
    int count = static_cast<int>(entities.size());

    // ImGui::Text("%d entit%s", count, count == 1 ? "y" : "ies");
    ImGui::Separator();

    // Deferred mutations: applied after the loop so we don't invalidate the
    // indices we're iterating over. Only one action per frame is supported;
    // that's fine because each comes from a user gesture.
    enum class PendingOp { None, AddEmpty, AddDirLight, AddPointLight, Delete, Duplicate };
    PendingOp pendingOp = PendingOp::None;
    int       pendingTarget = -1;

    // Pending drag/drop: either reparent (src becomes child of dst) or
    // sibling-reorder (src placed before/after dst inside its parent's
    // child list). Only one gesture fires per frame.
    enum class DropKind { None, MakeChild, SiblingBefore, SiblingAfter };
    DropKind dropKind = DropKind::None;
    int      dropSrc = -1;
    int      dropDst = -1;

    // Flat list, indented by depth so the parent/child hierarchy reads
    // visually without building a real tree widget yet.
    for (int i = 0; i < count; ++i) {
        int depth = 0;
        for (int p = entities[i].parent; p >= 0 && depth < 32; ++depth)
            p = entities[p].parent;
        if (depth > 0) ImGui::Indent(depth * 24.0f);

        ImGui::PushID(i);
        bool selected = (scene.selected == i);
        if (ImGui::Selectable(entities[i].name.c_str(), selected))
            scene.selected = (scene.selected == i) ? -1 : i;

        // Drag source: any entity except the root can be dragged onto another
        // to re-parent. The root can still be a drop target.
        if (entities[i].parent != -1 &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("TOON_ENTITY_IDX", &i, sizeof(int));
            ImGui::Text("%s", entities[i].name.c_str());
            ImGui::EndDragDropSource();
        }
        // Drop target: top 25% = insert above as sibling, bottom 25% = insert
        // below as sibling, middle 50% = make a child of this entity.
        if (ImGui::BeginDragDropTarget()) {
            const ImVec2 rmin = ImGui::GetItemRectMin();
            const ImVec2 rmax = ImGui::GetItemRectMax();
            const float  h = rmax.y - rmin.y;
            const float  y = ImGui::GetIO().MousePos.y - rmin.y;
            DropKind     kind = DropKind::MakeChild;
            if (entities[i].parent != -1) {
                if (y < h * 0.25f)      kind = DropKind::SiblingBefore;
                else if (y > h * 0.75f) kind = DropKind::SiblingAfter;
            }
            if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("TOON_ENTITY_IDX")) {
                dropSrc = *static_cast<const int*>(payload->Data);
                dropDst = i;
                dropKind = kind;
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click context menu.
        if (ImGui::BeginPopupContextItem("entity_ctx")) {
            if (ImGui::MenuItem("Add Empty Child")) {
                pendingOp = PendingOp::AddEmpty;
                pendingTarget = i;
            }
            if (ImGui::MenuItem("Add Directional Light")) {
                pendingOp = PendingOp::AddDirLight;
                pendingTarget = i;
            }
            if (ImGui::MenuItem("Add Point Light")) {
                pendingOp = PendingOp::AddPointLight;
                pendingTarget = i;
            }
            ImGui::Separator();
            bool isRoot = (entities[i].parent == -1);
            if (ImGui::MenuItem("Duplicate", nullptr, false, !isRoot)) {
                pendingOp = PendingOp::Duplicate;
                pendingTarget = i;
            }
            if (ImGui::MenuItem("Delete", nullptr, false, !isRoot)) {
                pendingOp = PendingOp::Delete;
                pendingTarget = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        if (depth > 0) ImGui::Unindent(depth * 24.0f);
    }

    // Apply deferred mutations after the list loop.
    switch (dropKind) {
    case DropKind::MakeChild:     ReparentEntity(scene, dropSrc, dropDst);             break;
    case DropKind::SiblingBefore: MoveEntityAsSibling(scene, dropSrc, dropDst, true);  break;
    case DropKind::SiblingAfter:  MoveEntityAsSibling(scene, dropSrc, dropDst, false); break;
    case DropKind::None: break;
    }
    switch (pendingOp) {
    case PendingOp::AddEmpty:
        scene.selected = AddChildEntity(scene, pendingTarget, "Empty");
        break;
    case PendingOp::AddDirLight:
        scene.selected = AddLightEntity(scene, pendingTarget,
            LightType::Directional);
        break;
    case PendingOp::AddPointLight:
        scene.selected = AddLightEntity(scene, pendingTarget, LightType::Point);
        break;
    case PendingOp::Duplicate: {
        int dup = DuplicateEntity(scene, pendingTarget);
        if (dup >= 0) scene.selected = dup;
        break;
    }
    case PendingOp::Delete:
        DeleteEntity(scene, pendingTarget);
        break;
    case PendingOp::None: break;
    }
    count = static_cast<int>(entities.size());  // list size may have changed

    ImGui::End();

    // -- Gizmo on selected entity ---------------------------------------------
    bool gizmoUsing = false;
    {
        int count = static_cast<int>(scene.entities.size());
        if (scene.selected >= 0 && scene.selected < count &&
            !scene.entities[scene.selected].light &&
            scene.entities[scene.selected].transform) {
            Entity& e = scene.entities[scene.selected];

            ImGuizmo::BeginFrame();
            ImGuiIO& gio = ImGui::GetIO();
            ImGuizmo::SetRect(0, 0, gio.DisplaySize.x, gio.DisplaySize.y);
            ImGuizmo::SetOrthographic(false);

            glm::mat4 view = CameraViewMatrix(camera);
            glm::mat4 proj = CameraProjectionMatrix(camera,
                gio.DisplaySize.x / std::max(gio.DisplaySize.y, 1.0f));

            // Manipulate in world space, but write back the local transform
            // relative to the parent so the hierarchy stays consistent.
            glm::mat4 parentW = (e.parent >= 0 && e.parent < count)
                ? scene.entities[e.parent].worldMatrix
                : glm::mat4(1.0f);
            glm::mat4 world = parentW * TransformMatrix(*e.transform);

            ImGuizmo::OPERATION op;
            switch (s.gizmoOp) {
            case 1:  op = ImGuizmo::ROTATE;    break;
            case 2:  op = ImGuizmo::SCALE;     break;
            default: op = ImGuizmo::TRANSLATE;  break;
            }
            ImGuizmo::MODE mode = s.gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            float snapVals[3] = { s.gizmoSnapValue, s.gizmoSnapValue, s.gizmoSnapValue };
            float* snap = s.gizmoSnap ? snapVals : nullptr;

            if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                op, mode, glm::value_ptr(world),
                nullptr, snap)) {
                glm::mat4 local = glm::inverse(parentW) * world;
                float t[3], r[3], sc[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), t, r, sc);
                e.transform->position = { t[0], t[1], t[2] };
                e.transform->rotation = { r[0], r[1], r[2] };
                e.transform->scale = { sc[0], sc[1], sc[2] };
            }

            gizmoUsing = ImGuizmo::IsUsing();
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard || gizmoUsing;
}

void OverlayShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
