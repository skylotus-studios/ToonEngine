#pragma once
//============================================================================
//  app/scene_roundtrip.h: --scene-roundtrip -- load/save idempotency check.
//
//  Sibling to app/sim_runtime.h and app/headless_render.h, same "one mode, one file" shape.
//  Loads a scene, saves it, loads what it just saved, saves THAT -- two on-disk .scene files
//  that should be byte-identical if nothing was silently dropped, reordered, or reformatted by
//  a round trip. Deliberately does NOT diff the two files itself: scripts/verify.py's
//  `scene_roundtrip` step does a plain text compare, the same split of responsibility
//  app/headless_render.h's captures (verify.py's `golden_diff.py`) already use.
//
//  No window, no device, ever -- core/scene/serializer.h's LoadScene/SaveScene only reach a
//  Renderer to upload meshes/models/textures, and Renderer::CreateMesh/LoadModel/LoadTexture all
//  early-out on HasDevice()==false (see renderer.h's own comment), the same device-less pattern
//  app/sim_runtime.h's --sim-only already proved safe.
//============================================================================
#include <string>

namespace toon {

    struct SceneRoundtripOptions {
        std::string scenePath;
        // pass1.scene / pass2.scene land here.
        std::string outDir = "artifacts/roundtrip";
    };

    // Returns a process exit code: 0 on success (both files written), 1 if the scene failed to
    // load or either save failed.
    int RunSceneRoundtrip(const SceneRoundtripOptions &opts);

} // namespace toon
