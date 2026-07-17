#pragma once
//============================================================================
//  ui/panels/dockspace.h — the full-window dockspace + one-time default panel layout.
//============================================================================
namespace toon {

    struct EditorState;

    // Full-window dock space with a see-through center so the scene shows through; panels dock
    // around it. Builds the default layout once (state.dockLayoutBuilt) -- after that, whatever
    // the user arranges sticks. No-op when built without IMGUI_HAS_DOCK. Call right after
    // DrawMenuBar's modals, before any of the dockable panels (Playback/Objects/Properties/
    // Settings/Contents).
    void SetupDockspace(EditorState &state);

} // namespace toon
