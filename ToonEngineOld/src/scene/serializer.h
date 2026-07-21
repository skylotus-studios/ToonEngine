// Scene serialization — save/load entity hierarchy to a text file.
//
// Format is a simple line-based text file (.scene). On save, entity
// transforms, lights, and model paths are written. On load, models
// are re-loaded from disk and the scene is rebuilt.

#pragma once

#include "scene/scene.h"
#include "scene/camera.h"

struct RenderSettings;  // forward — defined in ui/overlay.h

bool SaveScene(const char* path, const Scene& scene,
               const Camera& camera, const RenderSettings& settings);

bool LoadScene(const char* path, Scene& scene,
               Camera& camera, RenderSettings& settings,
               TextureHandle& defaultTexture);
