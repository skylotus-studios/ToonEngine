//============================================================================
//  app/scene_ops.cpp: see scene_ops.h.
//============================================================================
#include "app/scene_ops.h"

#include "app/editor_state.h"
#include "core/scene/serializer.h"

namespace toon {

    void LoadSceneInto(EditorState &state, const char *path) {
        if (LoadScene(path, state.runtime.scene, state.runtime.camera, state.runtime.renderer)) {
            state.sceneStatus = "Loaded.";
        } else {
            state.sceneStatus = "Load failed (see console).";
        }
    }

    void SaveSceneFrom(EditorState &state, const char *path) {
        state.sceneStatus = SaveScene(path, state.runtime.scene, state.runtime.camera) ? "Saved." : "Save failed (see console).";
    }

    void NewScene(EditorState &state) {
        DestroyScene(state.runtime.scene);
        EnsureSceneRoot(state.runtime.scene);
        state.runtime.scene.selected = -1;
        state.runtime.camera = state.cameraDefault;
        state.sceneStatus = "New scene.";
    }

} // namespace toon
