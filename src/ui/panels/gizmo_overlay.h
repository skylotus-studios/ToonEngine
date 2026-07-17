#pragma once
//============================================================================
//  ui/panels/gizmo_overlay.h — ImGuizmo hotkeys + the transform-gizmo overlay itself.
//============================================================================
namespace toon {

    struct EditorState;

    // Starts ImGuizmo's frame (must follow ImGui's own NewFrame, i.e. Renderer::BeginUI, and
    // precede any Manipulate call this frame), then handles the Unity-style W/E/R
    // move/rotate/scale and X local/world hotkeys. Gated so they don't fire while typing in a
    // field or while fly-navigating (right mouse held). Call right after Renderer::BeginUI,
    // before DrawMenuBar.
    void GizmoHotkeys(EditorState &state);

    // Manipulates the selected entity's world matrix; on edit, folds the parent back out and
    // decomposes to its local TRS (SetEntityWorldMatrix). No-op when nothing transform-bearing
    // is selected. Call after DrawPropertiesPanel (matches the panel's original ordering).
    void DrawGizmoOverlay(EditorState &state);

} // namespace toon
