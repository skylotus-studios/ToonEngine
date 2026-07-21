#pragma once
//============================================================================
//  app/scene_ops.h: scene load/save/new, writing EditorState::sceneStatus.
//
//  Free functions replacing main()'s old loadScene/newScene lambdas, so both
//  ui/panels/menu_bar.cpp (File menu + its Open/Save-As modals) and
//  ui/panels/file_browser.cpp's double-click-a-.scene handling (via main.cpp's Contents
//  glue) can call the same logic without each capturing a dozen EditorState fields.
//============================================================================
namespace toon {

    struct EditorState;

    // Load `path` into state.scene/state.camera (via state.renderer for mesh/model upload),
    // and set state.sceneStatus to reflect the result.
    void LoadSceneInto(EditorState &state, const char *path);

    // Save state.scene/state.camera to `path`, and set state.sceneStatus to reflect the result.
    void SaveSceneFrom(EditorState &state, const char *path);

    // File menu's "New Scene": drop every entity (GPU mesh/model handles stay alive but
    // unreferenced until Renderer::Shutdown, same as LoadSceneInto replacing the vector --
    // see core/scene/scene.h's DestroyScene), then restore just the root for an
    // empty-but-valid tree, and reset the camera to its startup default.
    void NewScene(EditorState &state);

} // namespace toon
