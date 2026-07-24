#pragma once
//============================================================================
//  core/save/savegame.h: the player save/progress file (roadmap #18).
//
//  DELIBERATELY NOT a scene dump. core/scene/serializer.* saves the level as the developer
//  authored it (every entity, its transform, materials); this saves what a PLAYER did in a
//  running game -- a small, separate "game-state document." Persisting a whole-scene snapshot
//  (the editor's in-memory Play/Stop `sceneBackup`, written to disk) would be the wrong thing:
//  it welds the save to the authored layout, so patching a level breaks every existing save,
//  and it stores a thousand untouched things to record the few the player changed. The shape
//  here is the one Unreal (USaveGame), Unity (a JsonUtility struct), and Godot (ConfigFile)
//  all use: a small document gameplay code writes explicit facts into.
//
//  Scope note (roadmap #18, "minimal plumbing" option): this builds the whole save MACHINE --
//  path resolution (core/platform/paths.h's UserData), slots, versioning, read/write, the
//  runtime New Game/Continue flow (app/save_glue.h) -- around a near-empty payload. The real
//  gameplay schema (inventory, unlock flags, checkpoints) is deferred until there's a game to
//  fill it: it lands as fields on SaveGame (and lines in the format) later, a data change, not
//  a rearchitecture. `gameBlob` is the opaque placeholder gameplay owns until then.
//
//  Diligent-free, plain-data + free functions, hand-rolled line-based text -- the same house
//  style (and no new dependency) as core/scene/serializer.*.
//============================================================================
#include <string>

namespace toon {

    // The player-progress document. Small and general on purpose (see the header comment): the
    // engine owns this container + its serialization; a game fills it. Bump kSaveVersion (below)
    // and handle the old value in ReadSave whenever the fields change.
    struct SaveGame {
        int version = 1;             // format version; ReadSave rejects anything newer than it knows
        std::string scenePath;       // which scene Continue should load (Assets-relative or absolute)
        float playtimeSeconds = 0.0f; // total time the player has spent in-game
        std::string gameBlob;        // opaque, game-owned; the real save schema replaces this later
    };

    // The version WriteSave stamps and the newest ReadSave understands. Kept next to SaveGame so
    // the two move together.
    constexpr int kSaveVersion = 1;

    // Write `s` to `path` (creating parent directories). Returns false (and logs to stderr) if
    // the file can't be opened. `path` is typically UserData::SaveSlot(n) (core/platform/paths.h).
    bool WriteSave(const std::string &path, const SaveGame &s);

    // Read a save from `path` into `out`. Returns false (leaving `out` untouched) and logs to
    // stderr if the file is missing, malformed, or a NEWER version than this build understands --
    // callers treat any false as "no usable save, start fresh" rather than crashing on it. Parses
    // into a side buffer and only assigns `out` on full success, the same failure contract as
    // core/scene/serializer.h's LoadScene.
    bool ReadSave(const std::string &path, SaveGame &out);

    // True if a save file exists for numbered `slot` (UserData::SaveSlot(slot)). Cheap existence
    // check for the title screen's "Continue" affordance; does not validate the file's contents.
    bool SaveExists(int slot);

    // Delete numbered `slot`'s save file if present. Returns true if a file was removed, false if
    // none existed or the removal failed. (No UI uses this yet; it completes the slot API.)
    bool DeleteSave(int slot);

} // namespace toon
