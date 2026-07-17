#pragma once
//============================================================================
//  ui/panels/menu_bar.h — the main menu bar (File / Edit / Tools / View / Help) and its
//  Open Scene / Save Scene As / About modals.
//============================================================================
namespace toon {

    struct EditorState;

    // Mirrors capabilities that already exist elsewhere (the Settings panel's Save/Load and
    // Reset camera, the hierarchy's right-click ops, the Inspector's gizmo controls) under the
    // menu layout a desktop editor is expected to have. Actions needing a path just queue a
    // modal (opened and drawn here too) instead of running inline.
    void DrawMenuBar(EditorState &state);

} // namespace toon
