//============================================================================
//  ui/panels/settings_panel.cpp: see settings_panel.h.
//============================================================================
#include "ui/panels/settings_panel.h"

#include "app/editor_state.h"
#include "core/input/input_system.h"

namespace toon {

    void DrawSettingsPanel(EditorState &state) {
        // debugOpen captures the pre-Begin value: see objects_panel.cpp's hierarchyOpen
        // comment for why.
        const bool debugOpen = state.showDebug;
        if (debugOpen && ImGui::Begin("Settings", &state.showDebug)) {
            ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

            // Gizmo editing settings (moved from Properties) -- Local space toggle, snap
            // toggle, and the active op's step size, squeezed to ~4 characters since this is a
            // quick-glance toolbar value, not a precision input.
            ImGui::SeparatorText("Snapping");
            bool localSpace = (state.gizmoMode == ImGuizmo::LOCAL);
            if (ImGui::Checkbox("Local", &localSpace)) {
                state.gizmoMode = localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &state.gizmoSnap);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
            if (state.gizmoOp == ImGuizmo::ROTATE) {
                ImGui::DragFloat("Angle", &state.snapRotateDeg, 1.0f, 1.0f, 90.0f, "%.0f");
            } else if (state.gizmoOp == ImGuizmo::SCALE) {
                ImGui::DragFloat("Scale", &state.snapScale, 0.05f, 0.001f, 10.0f, "%.2f");
            } else {
                ImGui::DragFloat("Move", &state.snapTranslate, 0.1f, 0.001f, 10.0f, "%.2f");
            }
            // Theme lives in the View menu, and Save/Load in the File menu + Contents
            // (double-click a .scene) -- both dropped here as redundant with those.
            ImGui::SeparatorText("Shader");
            ImGui::SliderFloat("Bands", &state.style.bands, 1.0f, 8.0f, "%.0f");
            ImGui::SliderFloat("Ambient", &state.style.ambient, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline Thickness", &state.outlineScale, 0.0f, 3.0f);
#ifdef TOON_SHADER_HOT_RELOAD
            // Roadmap #10: a fallback alongside the automatic file-watcher reload (already
            // running every frame in Renderer::BeginFrame) -- compiled out of a Release build
            // entirely, same as the watcher itself.
            if (ImGui::Button("Reload Now")) {
                const uint32_t n = state.renderer.ReloadShaders();
                state.shaderReloadStatus = "Reloaded " + std::to_string(n) + " state(s)";
            }
            if (!state.shaderReloadStatus.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", state.shaderReloadStatus.c_str());
            }
#endif

            ImGui::SeparatorText("Camera");
            ImGui::TextDisabled("Right-drag: orbit (+WASD/QE fly)");
            ImGui::TextDisabled("Mid-drag: pan | Scroll: zoom | F: focus");
            ImGui::TextDisabled(Input::GamepadCount() > 0
                                    ? "Gamepad: left stick fly, right stick orbit"
                                    : "Gamepad: left stick fly, right stick orbit (none connected)");
            ImGui::TextDisabled("Rebind: edit assets/input.json, then relaunch.");
            ImGui::SliderAngle("FOV", &state.camera.fovY, 20.0f, 100.0f);
            if (ImGui::Button("Reset camera")) { state.camera = state.cameraDefault; }
            ImGui::Checkbox("Run Scripts", &state.runScripts);

            ImGui::SeparatorText("Physics");
            ImGui::Checkbox("Show Colliders", &state.showColliders);

            ImGui::SeparatorText("Audio");
            if (ImGui::Checkbox("Mute", &state.audioMuted)) {
                state.audio.SetMasterVolume(state.audioMuted ? 0.0f : state.masterVolume);
            }
            ImGui::BeginDisabled(state.audioMuted);
            if (ImGui::SliderFloat("Master Volume", &state.masterVolume, 0.0f, 1.0f)) {
                state.audio.SetMasterVolume(state.masterVolume);
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Post Processing (HDR)");
            ImGui::Checkbox("Tone map (ACES)", &state.post.toneMap);
            ImGui::SliderFloat("Exposure", &state.post.exposure, 0.1f, 4.0f);

            ImGui::Checkbox("Bloom", &state.post.bloom);
            if (state.post.bloom) {
                ImGui::SliderFloat("Intensity", &state.post.bloomIntensity, 0.0f, 1.0f);
                ImGui::SliderFloat("Threshold", &state.post.bloomThreshold, 0.0f, 1.5f);
                ImGui::SliderFloat("Soft knee", &state.post.bloomSoftKnee, 0.0f, 1.0f);
                ImGui::SliderFloat("Bloom radius", &state.post.bloomRadius, 0.3f, 0.85f);
            }

            ImGui::Checkbox("SSAO", &state.post.ssao);
            if (state.post.ssao) {
                ImGui::SliderFloat("AO strength", &state.post.ssaoStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("AO radius", &state.post.ssaoRadius, 0.1f, 3.0f);
                ImGui::Checkbox("AO temporal (motion-vector denoise)", &state.post.ssaoTemporal);
            }

            ImGui::Checkbox("Shadows (cascaded shadow maps)", &state.post.shadows);

            ImGui::Checkbox("Depth of field", &state.post.dof);
            if (state.post.dof) {
                ImGui::SliderFloat("Focus distance", &state.post.dofFocusDist, 3.0f, 25.0f);
                ImGui::SliderFloat("Aperture (f-stop)", &state.post.dofFStop, 1.0f, 16.0f);
                ImGui::SliderFloat("Max blur (CoC)", &state.post.dofMaxCoC, 0.0f, 0.05f, "%.3f");
            }

            ImGui::Checkbox("TAA (softens toon edges)", &state.post.taa);

            ImGui::Checkbox("SSR (reflections in the ground)", &state.post.ssr);
            if (state.post.ssr) { ImGui::SliderFloat("Reflection strength", &state.post.ssrStrength, 0.0f, 1.5f); }
        }
        if (debugOpen) { ImGui::End(); }
    }

} // namespace toon
