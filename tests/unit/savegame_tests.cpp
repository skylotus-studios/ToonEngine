//============================================================================
//  tests/unit/savegame_tests.cpp: core/save/savegame.h round-trip + version-reject guard.
//
//  "SaveGame compat" (deep tier, scripts/verify.py's `savegame_compat` step; CTest filter
//  "SaveGame") given there's no real old-format save to test against yet (SaveGame is at
//  kSaveVersion == 1 with no history): the compat surface that actually exists today is (a)
//  WriteSave->ReadSave preserving every field exactly, and (b) ReadSave correctly refusing a
//  file claiming a newer version than this build understands, per its own documented contract
//  ("leaving `out` untouched... callers treat any false as 'no usable save, start fresh'").
//============================================================================
#include "core/save/savegame.h"
#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace toon;

namespace {

    // A scratch path under the build directory (CWD when CTest runs this binary), not a real
    // UserData save slot -- WriteSave/ReadSave take a plain path, no UserData resolver needed
    // for this test (see savegame.h's own comment: "typically UserData::SaveSlot(n)", not
    // required to be).
    std::string ScratchPath(const char *name) {
        std::filesystem::create_directories("artifacts/unit-test-scratch");
        return std::string("artifacts/unit-test-scratch/") + name;
    }

} // namespace

TOON_TEST("SaveGame.RoundTripExact") {
    SaveGame original;
    original.version = kSaveVersion;
    original.scenePath = "assets/scenes/smoke.scene";
    original.playtimeSeconds = 1234.5f;
    original.gameBlob = "some=opaque;game=owned;blob=data";

    const std::string path = ScratchPath("roundtrip.save");
    CHECK(WriteSave(path, original));

    SaveGame loaded;
    CHECK(ReadSave(path, loaded));
    CHECK(loaded.version == original.version);
    CHECK(loaded.scenePath == original.scenePath);
    CHECK_NEAR(loaded.playtimeSeconds, original.playtimeSeconds, 1e-4);
    CHECK(loaded.gameBlob == original.gameBlob);
}

TOON_TEST("SaveGame.RejectsNewerVersion") {
    // Hand-written, not round-tripped through WriteSave: this is deliberately a save file
    // claiming a version (999) newer than this build's kSaveVersion. Matches ReadSave's own
    // format exactly (savegame.cpp's own banner: "version <n>", "scene <path>",
    // "playtime <seconds>", "blob <byteCount>" + raw bytes) -- if that format ever changes,
    // this needs updating alongside it, the same coupling core/scene/serializer.cpp's own
    // hand-authored .scene fixtures already have.
    const std::string path = ScratchPath("future-version.save");
    {
        std::ofstream f(path, std::ios::binary);
        f << "version 999\n";
        f << "scene assets/scenes/default.scene\n";
        f << "playtime 1.000000\n";
        f << "blob 0\n";
    }

    SaveGame out;
    out.scenePath = "sentinel-untouched";
    const bool ok = ReadSave(path, out);
    CHECK(!ok);
    // ReadSave's own documented contract: `out` is left untouched on failure, not partially
    // overwritten.
    CHECK(out.scenePath == "sentinel-untouched");
}
