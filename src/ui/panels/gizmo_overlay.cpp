//============================================================================
//  ui/panels/gizmo_overlay.cpp: see gizmo_overlay.h.
//============================================================================
#include "ui/panels/gizmo_overlay.h"

#include "app/editor_state.h"

namespace toon {

    void GizmoHotkeys(EditorState &state) {
        ImGuizmo::BeginFrame(); // must follow ImGui's NewFrame (Renderer::BeginUI), before Manipulate

        const ImGuiIO &io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { state.gizmoOp = ImGuizmo::TRANSLATE; }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { state.gizmoOp = ImGuizmo::ROTATE; }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { state.gizmoOp = ImGuizmo::SCALE; }
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                state.gizmoMode = (state.gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
            }
        }
    }

    void DrawGizmoOverlay(EditorState &state) {
        Scene &scene = state.scene;
        if (scene.selected > 0 && scene.selected < static_cast<int>(scene.entities.size()) &&
            scene.entities[scene.selected].transform) {
            const ImGuiIO &io = ImGui::GetIO();
            // Diligent's row-major matrices feed ImGuizmo (column-major) directly: the
            // conventions are transposes, so the raw 16 floats already match, no explicit
            // transpose needed.
            Mat4 view, proj;
            state.renderer.GetViewProj(view, proj);
            ImGuizmo::SetOrthographic(state.camera.orthographic);
            ImGuizmo::AllowAxisFlip(false); // show true axis directions (don't auto-face camera)
            ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
            ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
            Mat4 world = scene.entities[scene.selected].worldMatrix;
            const bool snapping = state.gizmoSnap || io.KeyCtrl;
            const float step = (state.gizmoOp == ImGuizmo::ROTATE)  ? state.snapRotateDeg
                               : (state.gizmoOp == ImGuizmo::SCALE) ? state.snapScale
                                                                    : state.snapTranslate;
            const float snapVec[3] = {step, step, step};
            // 2D editor mode (roadmap #14): drop the third axis from the on-screen handle --
            // translate-Z, rotate-X/Y, and scale-Z all move/turn out of the flat view plane,
            // which a locked-orthographic camera can't usefully show. The Properties panel's
            // Position/Rotation/Scale fields stay fully editable regardless (matching Unity's
            // own 2D-mode behavior: the viewport handle loses the axis, the inspector doesn't).
            ImGuizmo::OPERATION op = state.gizmoOp;
            if (state.camera.orthographic) {
                op = static_cast<ImGuizmo::OPERATION>(
                    op & ~(ImGuizmo::TRANSLATE_Z | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::SCALE_Z));
            }
            if (ImGuizmo::Manipulate(view.m, proj.m, op, state.gizmoMode, world.m, nullptr,
                                     snapping ? snapVec : nullptr)) {
                SetEntityWorldMatrix(scene, scene.selected, world);
            }
        }
    }

} // namespace toon
