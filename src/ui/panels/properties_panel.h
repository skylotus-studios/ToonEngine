#pragma once
//============================================================================
//  ui/panels/properties_panel.h: the "Properties" inspector for the selected entity.
//============================================================================
namespace toon {

    struct EditorState;

    // Edits the selected entity's name / transform / material / light / collider / rigid body
    // / scripts. Shows a placeholder message when nothing is selected.
    void DrawPropertiesPanel(EditorState &state);

} // namespace toon
