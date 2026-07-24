#pragma once
//============================================================================
//  core/platform/paths.h: exe-relative asset path resolution.
//
//  Every asset the engine loads (shaders, models, fonts, scenes, audio, sprites, the window
//  icon, the input-bindings file) lives under one `assets/` directory. This module resolves
//  that directory ONCE at startup and hands out the per-category subpaths built from it, so
//  no absolute build-machine path is baked into the binary and a copied exe finds its assets
//  wherever it sits (roadmap #16: asset packaging for a shippable build).
//
//  Diligent-free by design (std::string in, std::string out), like core/math.h: main.cpp,
//  app/, and ui/panels/ all include it while staying backend-agnostic, and renderer.cpp
//  consumes it from the engine side. It is a standalone engine utility, not part of the
//  renderer seam.
//============================================================================
#include <string>

namespace toon {
    namespace Assets {

        // Resolve the asset root ONCE, before any accessor below is called -- first thing in
        // main()/player_main(), ahead of even the default-scene path, which is built from
        // Scenes(). Prefers <exe_dir>/assets (a packaged/shipped build, where CMake's install
        // rules stage assets next to the exe); falls back to the source-tree assets/ path CMake
        // bakes in as TOON_ASSET_ROOT when that folder isn't there (running straight out of
        // build/<preset>/ during development, where nothing copied assets next to the exe). One
        // binary serves both, with no call-site branching.
        void Init();

        // The resolved assets/ directory, no trailing slash (also the Asset Browser's root).
        // Empty until Init() has run.
        const std::string &Root();

        // Per-category directories (no trailing slash), each Root() + a fixed subpath.
        std::string Shaders();
        std::string Models();
        std::string Fonts();
        std::string Scenes();
        std::string Audio();
        std::string Sprites();

        // Individual well-known files.
        std::string Icon();      // the window icon (sprites/icon.png)
        std::string InputJson(); // the editor's saved key bindings (input.json)

        // A single sprite texture by its user-facing filename (e.g. "icon.png"), resolved
        // against Sprites(). Generalizes the old inline SpriteTexturePath helper that used to
        // live in core/scene/scene.h.
        std::string Sprite(const std::string &file);

    } // namespace Assets
} // namespace toon
