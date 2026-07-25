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

        // A single scene by its user-facing filename (e.g. "level_two.scene"), resolved against
        // Scenes(). The twin of Sprite() above, and for the same reason: a level transition
        // (roadmap #19) names its destination by FILENAME in the .scene file, so an authored
        // level never bakes in a build-machine path and still resolves inside a packaged build.
        std::string Scene(const std::string &file);

    } // namespace Assets

    // Writable per-user data (roadmap #18), the WRITE-side twin of the read-only Assets tree
    // above. Assets:: resolves where the game reads its content from; UserData:: resolves where
    // it writes what the player produces (save files today). The split is deliberate: a shipped
    // build's assets/ can sit under a read-only install directory (e.g. Program Files), so saves
    // must go to an OS-blessed per-user location that stays writable regardless. Diligent-free,
    // same as Assets. Roadmap #16 explicitly deferred introducing this to the save system.
    namespace UserData {

        // The per-user writable root for this app, created if missing:
        //   Windows: %LOCALAPPDATA%/ToonEngine  (e.g. C:/Users/<name>/AppData/Local/ToonEngine)
        //   Linux:   $XDG_DATA_HOME/ToonEngine, or ~/.local/share/ToonEngine
        //   macOS:   empty for now (stub, like Assets' ExecutableDir; filled when that port lands)
        // Empty string on failure (query failed, or the dir couldn't be created). Unlike
        // Assets::Root() there's no Init()/cache: this is called rarely (a save/load), so it just
        // resolves each time.
        std::string Root();

        // The directory save files live in: Root()/saves/local, created if missing. The `local`
        // segment is a deliberate seam for Steam (roadmap #25): it becomes the player's
        // {64BitSteamID} once Steamworks is wired, at which point Steam Auto-Cloud (partner-site
        // config, not code) syncs the saves/ subtree per user. Empty string if Root() failed.
        std::string SaveDir();

        // The path of one numbered save slot: SaveDir()/slotN.save. Empty if SaveDir() failed.
        std::string SaveSlot(int slot);

    } // namespace UserData
} // namespace toon
