//============================================================================
//  ui/panels/settings_panel.cpp: see settings_panel.h.
//============================================================================
#include "ui/panels/settings_panel.h"

#include "app/editor_state.h"
#include "app/editor_tick.h" // SetEditorMode2D
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
            ImGui::SliderFloat("Bands", &state.runtime.style.bands, 1.0f, 8.0f, "%.0f");
            ImGui::SliderFloat("Ambient", &state.runtime.style.ambient, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline Thickness", &state.runtime.outlineScale, 0.0f, 3.0f);
#ifdef TOON_SHADER_HOT_RELOAD
            // Roadmap #10: a fallback alongside the automatic file-watcher reload (already
            // running every frame in Renderer::BeginFrame) -- compiled out of a Release build
            // entirely, same as the watcher itself.
            if (ImGui::Button("Reload Now")) {
                const uint32_t n = state.runtime.renderer.ReloadShaders();
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
            ImGui::SliderAngle("FOV", &state.runtime.camera.fovY, 20.0f, 100.0f);
            if (ImGui::Button("Reset camera")) { state.runtime.camera = state.cameraDefault; }
            // 2D editor mode (roadmap #14): locks the viewport to an orthographic view facing
            // the sprite plane, for working on sprite-heavy scenes. Routed through
            // SetEditorMode2D (app/editor_tick.h), not a direct write to camera.orthographic,
            // so the 3D angle save/restore always happens together with the flag.
            bool orthographic2D = state.runtime.camera.orthographic;
            if (ImGui::Checkbox("2D Mode", &orthographic2D)) { SetEditorMode2D(state, orthographic2D); }
            ImGui::Checkbox("Run Scripts", &state.runtime.runScripts);

            ImGui::SeparatorText("Physics");
            ImGui::Checkbox("Show Colliders", &state.showColliders);

            ImGui::SeparatorText("Environment");
            ImGui::Checkbox("Grid", &state.showGrid);
            ImGui::Checkbox("Sky Gradient", &state.runtime.showSky);
            if (state.runtime.showSky) {
                ImGui::ColorEdit3("Sky Top", &state.runtime.skyTop.r);
                ImGui::ColorEdit3("Sky Bottom", &state.runtime.skyBottom.r);
            }

            ImGui::SeparatorText("Audio");
            if (ImGui::Checkbox("Mute", &state.runtime.audioMuted)) {
                state.runtime.audio.SetMasterVolume(state.runtime.audioMuted ? 0.0f : state.runtime.masterVolume);
            }
            ImGui::BeginDisabled(state.runtime.audioMuted);
            if (ImGui::SliderFloat("Master Volume", &state.runtime.masterVolume, 0.0f, 1.0f)) {
                state.runtime.audio.SetMasterVolume(state.runtime.masterVolume);
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Post Processing (HDR)");
            ImGui::Checkbox("Tone map (ACES)", &state.runtime.post.toneMap);
            ImGui::SliderFloat("Exposure", &state.runtime.post.exposure, 0.1f, 4.0f);

            ImGui::Checkbox("Bloom", &state.runtime.post.bloom);
            if (state.runtime.post.bloom) {
                ImGui::SliderFloat("Intensity", &state.runtime.post.bloomIntensity, 0.0f, 1.0f);
                ImGui::SliderFloat("Threshold", &state.runtime.post.bloomThreshold, 0.0f, 1.5f);
                ImGui::SliderFloat("Soft knee", &state.runtime.post.bloomSoftKnee, 0.0f, 1.0f);
                ImGui::SliderFloat("Bloom radius", &state.runtime.post.bloomRadius, 0.3f, 0.85f);
            }

            ImGui::Checkbox("SSAO", &state.runtime.post.ssao);
            if (state.runtime.post.ssao) {
                ImGui::SliderFloat("AO strength", &state.runtime.post.ssaoStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("AO radius", &state.runtime.post.ssaoRadius, 0.1f, 3.0f);
                ImGui::Checkbox("AO temporal (motion-vector denoise)", &state.runtime.post.ssaoTemporal);
            }

            ImGui::Checkbox("Shadows (cascaded shadow maps)", &state.runtime.post.shadows);

            ImGui::Checkbox("Depth of field", &state.runtime.post.dof);
            if (state.runtime.post.dof) {
                ImGui::SliderFloat("Focus distance", &state.runtime.post.dofFocusDist, 3.0f, 25.0f);
                ImGui::SliderFloat("Aperture (f-stop)", &state.runtime.post.dofFStop, 1.0f, 16.0f);
                ImGui::SliderFloat("Max blur (CoC)", &state.runtime.post.dofMaxCoC, 0.0f, 0.05f, "%.3f");
            }

            ImGui::Checkbox("TAA (softens toon edges)", &state.runtime.post.taa);

            ImGui::Checkbox("SSR (reflections in the ground)", &state.runtime.post.ssr);
            if (state.runtime.post.ssr) { ImGui::SliderFloat("Reflection strength", &state.runtime.post.ssrStrength, 0.0f, 1.5f); }
        }
        if (debugOpen) { ImGui::End(); }
    }

} // namespace toon
