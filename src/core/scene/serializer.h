#pragma once
//============================================================================
//  core/scene/serializer.h: scene save/load to a simple text .scene file.
//
//  Adapted from ToonEngineOld/src/scene/serializer.* for this engine's entity shape: a
//  renderable is either a procedural primitive (no source file, regenerated on load from a
//  saved PrimitiveDesc, see core/rendering/primitives.h) or a loaded glTF model (reloaded from a saved
//  path), never both. Diligent-free like scene.h/camera.h: LoadScene only reaches Diligent
//  indirectly, through Renderer::CreateMesh/LoadModel across the seam.
//============================================================================
#include "core/scene/scene.h"        // Scene, Entity
#include "core/rendering/renderer.h" // Camera, Renderer

namespace toon {

    // Save `scene` + the editor `camera` to `path` (plain text, human-readable/diffable).
    // Creates any missing parent directories. Returns false (and logs to stderr) if the file
    // can't be opened for writing.
    bool SaveScene(const char *path, const Scene &scene, const Camera &camera);

    // Load a scene from `path`, replacing `scene` and `camera` entirely on success. Procedural
    // entities rebuild their mesh via `renderer.CreateMesh` from the saved PrimitiveDesc; model
    // entities reload via `renderer.LoadModel`; scripts reconstruct via the name registry (see
    // core/scene/script.h). Returns false (and logs to stderr, leaving `scene`/`camera` untouched) if
    // the file can't be opened. Resets `scene.selected` to -1.
    bool LoadScene(const char *path, Scene &scene, Camera &camera, Renderer &renderer);

    // The same load, but into a caller-owned destination rather than the scene the engine is
    // currently running -- LoadScene is now a thin wrapper over this. Exists so a level
    // transition (roadmap #19, app/session.h) can parse and upload the INCOMING level while the
    // outgoing one is still live and playing, and only tear the old one down once this has
    // returned true. A failed load then costs nothing: no bodies destroyed, no sounds stopped,
    // no entities dropped, the player still standing in the level they were in. Unreal states
    // the same constraint for seamless travel ("there must always be a world loaded, so we can't
    // free the old map before loading the new one").
    //
    // Fills `out`/`outCamera` only on full success, same failure contract as LoadScene; note it
    // DOES create GPU resources through `renderer` either way, so a partial parse that fails
    // later can still leave meshes/models uploaded (see roadmap #20 for their lifetime).
    // `outCamera` seeds the fallback view a scene file's own camera lines then override.
    bool LoadSceneData(const char *path, Scene &out, Camera &outCamera, Renderer &renderer);

} // namespace toon
