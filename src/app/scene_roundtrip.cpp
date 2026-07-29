//============================================================================
//  app/scene_roundtrip.cpp: see scene_roundtrip.h.
//============================================================================
#include "app/scene_roundtrip.h"

#include "core/rendering/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/scripts/builtin_scripts.h" // RegisterBuiltinScripts
#include "core/scene/serializer.h"

#include <cstdio>
#include <filesystem>

namespace toon {

    int RunSceneRoundtrip(const SceneRoundtripOptions &opts) {
        // Scripts reconstruct through the name registry on load; same first-thing-in-bring-up
        // call --sim-only and InitRuntime both make.
        RegisterBuiltinScripts();

        std::error_code ec;
        std::filesystem::create_directories(opts.outDir, ec);

        // Never Init()'d: HasDevice() stays false for this whole run, so LoadScene's
        // renderer.CreateMesh/LoadModel/LoadTexture calls all early-out instead of touching a
        // device -- see this file's own header comment.
        Renderer renderer;

        Scene scene1;
        Camera camera1;
        if (!LoadScene(opts.scenePath.c_str(), scene1, camera1, renderer)) {
            std::fprintf(stderr, "--scene-roundtrip: failed to load '%s'\n", opts.scenePath.c_str());
            return 1;
        }

        const std::string pass1 = opts.outDir + "/pass1.scene";
        if (!SaveScene(pass1.c_str(), scene1, camera1)) {
            std::fprintf(stderr, "--scene-roundtrip: failed to write '%s'\n", pass1.c_str());
            return 1;
        }

        Scene scene2;
        Camera camera2;
        if (!LoadScene(pass1.c_str(), scene2, camera2, renderer)) {
            std::fprintf(stderr, "--scene-roundtrip: failed to reload '%s'\n", pass1.c_str());
            return 1;
        }

        const std::string pass2 = opts.outDir + "/pass2.scene";
        if (!SaveScene(pass2.c_str(), scene2, camera2)) {
            std::fprintf(stderr, "--scene-roundtrip: failed to write '%s'\n", pass2.c_str());
            return 1;
        }

        std::printf("--scene-roundtrip: wrote %s and %s\n", pass1.c_str(), pass2.c_str());
        return 0;
    }

} // namespace toon
