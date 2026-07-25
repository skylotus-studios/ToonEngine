#pragma once
//============================================================================
//  app/save_glue.h: bridges the save file (core/save/savegame.h) to the live RuntimeState.
//
//  savegame.* is engine-generic (a SaveGame struct + read/write, knowing nothing about the
//  runtime). This thin app-layer glue is where the two meet: it fills a SaveGame FROM the
//  running game and applies a loaded one BACK onto it. Runtime-side (takes RuntimeState&, no
//  ImGui), so ToonPlayer links it and the player's title New Game / Continue + F5 flow
//  (app/app_state.cpp) drives it.
//============================================================================
#include "core/save/savegame.h"

namespace toon {

    struct RuntimeState;

    // The single slot the runtime's New Game / Continue / F5 flow reads and writes. The slot API
    // (savegame.h's SaveExists/DeleteSave, UserData::SaveSlot) is N-slot ready; a multi-slot UI
    // waits on the in-game UI (roadmap #17), so today one named slot is enough.
    constexpr int kQuickSaveSlot = 0;

    // Snapshot the current run into a SaveGame: the active scene, elapsed playtime. gameBlob stays
    // empty until a real save schema exists (see savegame.h's scope note).
    SaveGame MakeSave(const RuntimeState &rs);

    // Apply a loaded SaveGame back onto `rs`: point the pending scene at the saved one and restore
    // playtime. The caller then enters AppState::Loading, which brings that scene up.
    void ApplySave(RuntimeState &rs, const SaveGame &s);

    // MakeSave + WriteSave to the quick-save slot. False (logged) if there's no writable location
    // or the write fails.
    bool QuickSave(RuntimeState &rs);

    // ReadSave the quick-save slot + ApplySave. False if there's no valid save (missing, corrupt,
    // or newer-version) -- the caller treats that as "start a fresh game."
    bool QuickLoad(RuntimeState &rs);

} // namespace toon
