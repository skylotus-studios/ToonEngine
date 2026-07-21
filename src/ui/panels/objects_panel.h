#pragma once
//============================================================================
//  ui/panels/objects_panel.h: the "Objects" hierarchy panel.
//============================================================================
namespace toon {

    struct EditorState;

    // A flat list over state.scene.entities (parents always precede children), indented by
    // depth so it reads as a tree: select / add child / duplicate / delete / drag-drop
    // reparent.
    void DrawObjectsPanel(EditorState &state);

} // namespace toon
