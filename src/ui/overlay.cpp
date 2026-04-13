// ImGui debug overlay implementation.

#include "overlay.h"

#include "core/animator.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

void OverlayInit(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Slightly transparent background so the scene shows through.
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 0.92f;

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

bool OverlayRender(RenderSettings& s, Scene& scene, float fps) {
    // -- Render settings panel ------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("ToonEngine");

    ImGui::Text("%.1f FPS (%.2f ms)", fps, (fps > 0.0f) ? 1000.0f / fps : 0.0f);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Toon Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Band High", &s.bandThresholdHigh, -1.0f, 1.0f);
        ImGui::SliderFloat("Band Low", &s.bandThresholdLow, -1.0f, 1.0f);
        ImGui::SliderFloat("Bright", &s.brightIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Mid", &s.midIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Shadow", &s.shadowIntensity, 0.0f, 1.0f);
        ImGui::ColorEdit3("Shadow Tint", &s.shadowTint.x);
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

    if (ImGui::CollapsingHeader("Scene")) {
        ImGui::ColorEdit3("Background", &s.clearColor.x);
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
            int triCount  = 0;
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

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void OverlayShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
