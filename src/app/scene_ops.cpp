//============================================================================
//  app/scene_ops.cpp: see scene_ops.h.
//============================================================================
#include "app/scene_ops.h"

#include "app/editor_state.h"
#include "core/scene/serializer.h"

namespace toon {

    void LoadSceneInto(EditorState &state, const char *path) {
        if (LoadScene(path, state.scene, state.camera, state.renderer)) {
            state.sceneStatus = "Loaded.";
        } else {
            state.sceneStatus = "Load failed (see console).";
        }
    }

    void SaveSceneFrom(EditorState &state, const char *path) {
        state.sceneStatus = SaveScene(path, state.scene, state.camera) ? "Saved." : "Save failed (see console).";
    }

    void NewScene(EditorState &state) {
        DestroyScene(state.scene);
        EnsureSceneRoot(state.scene);
        state.scene.selected = -1;
        state.camera = state.cameraDefault;
        state.sceneStatus = "New scene.";
    }

} // namespace toon
