// ImGui debug overlay implementation.

#include "overlay.h"

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

bool OverlayRender(RenderSettings& s, float fps) {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("ToonEngine");

    ImGui::Text("%.1f FPS (%.2f ms)", fps, (fps > 0.0f) ? 1000.0f / fps : 0.0f);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Toon Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Light Dir", &s.lightDir.x, 0.01f, -1.0f, 1.0f);
        ImGui::SliderFloat("Band High", &s.bandThresholdHigh, -1.0f, 1.0f);
        ImGui::SliderFloat("Band Low", &s.bandThresholdLow, -1.0f, 1.0f);
        ImGui::SliderFloat("Bright", &s.brightIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Mid", &s.midIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Shadow", &s.shadowIntensity, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Outlines", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Width", &s.outlineWidth, 0.0f, 2.0f);
        ImGui::ColorEdit4("Color", &s.outlineColor.x);
    }

    if (ImGui::CollapsingHeader("Scene")) {
        ImGui::ColorEdit3("Background", &s.clearColor.x);
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
