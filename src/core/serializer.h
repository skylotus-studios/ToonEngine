#pragma once
//============================================================================
//  core/serializer.h — scene save/load to a simple text .scene file.
//
//  Adapted from ToonEngineOld/src/scene/serializer.* for this engine's entity shape: a
//  renderable is either a procedural primitive (no source file — regenerated on load from a
//  saved PrimitiveDesc, see core/primitives.h) or a loaded glTF model (reloaded from a saved
//  path), never both. Diligent-free like scene.h/camera.h: LoadScene only reaches Diligent
//  indirectly, through Renderer::CreateMesh/LoadModel across the seam.
//============================================================================
#include "core/scene.h"      // Scene, Entity
#include "core/renderer.h"   // Camera, Renderer

namespace toon {

// Save `scene` + the editor `camera` to `path` (plain text, human-readable/diffable).
// Creates any missing parent directories. Returns false (and logs to stderr) if the file
// can't be opened for writing.
bool SaveScene(const char* path, const Scene& scene, const Camera& camera);

// Load a scene from `path`, replacing `scene` and `camera` entirely on success. Procedural
// entities rebuild their mesh via `renderer.CreateMesh` from the saved PrimitiveDesc; model
// entities reload via `renderer.LoadModel`; scripts reconstruct via the name registry (see
// core/script.h). Returns false (and logs to stderr, leaving `scene`/`camera` untouched) if
// the file can't be opened. Resets `scene.selected` to -1.
bool LoadScene(const char* path, Scene& scene, Camera& camera, Renderer& renderer);

} // namespace toon
