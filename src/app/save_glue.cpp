//============================================================================
//  app/save_glue.cpp: see save_glue.h.
//============================================================================
#include "app/save_glue.h"

#include "app/runtime_state.h"
#include "core/platform/paths.h" // UserData::SaveSlot

#include <cstdio>
#include <string>

namespace toon {

    SaveGame MakeSave(const RuntimeState &rs) {
        SaveGame s;
        s.version = kSaveVersion;
        s.scenePath = rs.pendingScenePath; // the scene the runtime brought up (Loading seeds it)
        s.playtimeSeconds = rs.playtimeSeconds;
        // s.gameBlob stays empty: no gameplay schema to record yet (savegame.h scope note).
        return s;
    }

    void ApplySave(RuntimeState &rs, const SaveGame &s) {
        rs.pendingScenePath = s.scenePath; // Loading (entered next) rebuilds the queue from this
        rs.playtimeSeconds = s.playtimeSeconds;
    }

    bool QuickSave(RuntimeState &rs) {
        const std::string path = UserData::SaveSlot(kQuickSaveSlot);
        if (path.empty()) {
            std::fprintf(stderr, "QuickSave: no writable save location\n");
            return false;
        }
        return WriteSave(path, MakeSave(rs));
    }

    bool QuickLoad(RuntimeState &rs) {
        const std::string path = UserData::SaveSlot(kQuickSaveSlot);
        if (path.empty()) { return false; }
        SaveGame s;
        if (!ReadSave(path, s)) { return false; }
        ApplySave(rs, s);
        return true;
    }

} // namespace toon
