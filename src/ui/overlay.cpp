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

static Theme sCurrentTheme = Theme::GrayStone;

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
                   Texture& defaultTexture, float fps) {
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

    ImGui::Begin("ToonEngine");

    ImGui::Text("%.1f FPS (%.2f ms)", fps, (fps > 0.0f) ? 1000.0f / fps : 0.0f);
    ImGui::Separator();

    // Gizmo toolbar: W=Translate, E=Rotate, R=Scale.
    {
        bool t = (s.gizmoOp == 0), r = (s.gizmoOp == 1), sc = (s.gizmoOp == 2);
        if (ImGui::RadioButton("W Move", t))  s.gizmoOp = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("E Rot", r))   s.gizmoOp = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("R Scl", sc))  s.gizmoOp = 2;
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
        ImGui::SliderFloat("Hull Width", &s.outlineWidth, 0.0f, 2.0f);
        ImGui::ColorEdit4("Hull Color", &s.outlineColor.x);
        ImGui::Checkbox("Even Thickness", &s.outlineScreenSpace);
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##shadow", &s.shadowEnabled);
        ImGui::SliderFloat("Bias", &s.shadowBias, 0.0f, 0.01f, "%.4f");
        ImGui::SliderFloat("Distance", &s.shadowDistance, 1.0f, 100.0f);
        ImGui::Text("4 cascades, 2048x2048");
    }

    if (ImGui::CollapsingHeader("Edge Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &s.edgeEnabled);
        ImGui::ColorEdit3("Edge Color", &s.edgeColor.x);
        ImGui::SliderFloat("Threshold", &s.edgeThreshold, 0.001f, 0.2f, "%.3f");
        ImGui::SliderFloat("Edge Width", &s.edgeWidth, 0.5f, 5.0f);
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

    ImGui::Begin("Entities");

    auto& entities = scene.entities;
    int count = static_cast<int>(entities.size());

    ImGui::Text("%d entit%s", count, count == 1 ? "y" : "ies");
    ImGui::Separator();

    // Selectable list.
    for (int i = 0; i < count; ++i) {
        bool selected = (scene.selected == i);
        if (ImGui::Selectable(entities[i].name.c_str(), selected))
            scene.selected = (scene.selected == i) ? -1 : i;
    }

    ImGui::Separator();

    // Inspector for selected entity.
    if (scene.selected >= 0 && scene.selected < count) {
        Entity& e = entities[scene.selected];

        // Light entity inspector.
        if (e.light) {
            Light& L = *e.light;
            ImGui::Text("Light: %s", LightTypeLabel(L.type));
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
                ImGui::DragFloat3("Position", &e.transform.position.x, 0.05f);
                ImGui::SliderFloat("Radius", &L.radius, 0.1f, 50.0f);
            }
            ImGui::Separator();
        }

        // Mesh entity inspector.
        if (!e.light) {
            ImGui::Text("Shading: %s", ShadingModeLabel(e.shading));

            int meshCount = 0;
            int triCount = 0;
            for (auto& sm : e.subMeshes) {
                ++meshCount;
                int n = sm.mesh.indexCount > 0 ? sm.mesh.indexCount : sm.mesh.vertexCount;
                triCount += n / 3;
            }
            ImGui::Text("Meshes: %d  |  Triangles: %d", meshCount, triCount);
            ImGui::Separator();

            ImGui::DragFloat3("Position", &e.transform.position.x, 0.05f);
            ImGui::DragFloat3("Rotation", &e.transform.rotation.x, 0.5f);
            ImGui::DragFloat3("Scale", &e.transform.scale.x, 0.01f, 0.01f, 100.0f);
        }

        // Animation controls (only for skinned entities with clips).
        if (e.skinned && !e.clips.empty()) {
            ImGui::Separator();
            ImGui::Text("Joints: %d  |  Clips: %d",
                static_cast<int>(e.skeleton.joints.size()),
                static_cast<int>(e.clips.size()));

            // Clip selector.
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
    } else {
        ImGui::TextDisabled("Select an entity to inspect");
    }

    ImGui::End();

    // -- Gizmo on selected entity ---------------------------------------------
    bool gizmoUsing = false;
    {
        int count = static_cast<int>(scene.entities.size());
        if (scene.selected >= 0 && scene.selected < count &&
            !scene.entities[scene.selected].light) {
            Entity& e = scene.entities[scene.selected];

            ImGuizmo::BeginFrame();
            ImGuiIO& gio = ImGui::GetIO();
            ImGuizmo::SetRect(0, 0, gio.DisplaySize.x, gio.DisplaySize.y);
            ImGuizmo::SetOrthographic(false);

            glm::mat4 view = CameraViewMatrix(camera);
            glm::mat4 proj = CameraProjectionMatrix(camera,
                gio.DisplaySize.x / std::max(gio.DisplaySize.y, 1.0f));
            glm::mat4 model = TransformMatrix(e.transform);

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
                                     op, mode, glm::value_ptr(model),
                                     nullptr, snap)) {
                // Decompose back to entity transform.
                float t[3], r[3], sc[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, sc);
                e.transform.position = {t[0], t[1], t[2]};
                e.transform.rotation = {r[0], r[1], r[2]};
                e.transform.scale    = {sc[0], sc[1], sc[2]};
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
