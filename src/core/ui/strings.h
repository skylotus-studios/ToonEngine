#pragma once
//============================================================================
//  core/ui/strings.h: player-facing string table (roadmap #17, localization-readiness).
//
//  Every player-facing string is addressed by a key from the StrId enum, never a raw literal at
//  the draw site, so localization (roadmap #32) becomes a data change -- swap the table -- not a
//  rewrite of every menu. The default English values live in strings.cpp; LoadStrings overrides
//  them from a per-language `key = value` data file (the #32 hook), keyed by the stable names
//  KeyName returns. Prefer this generated-constant form over magic string keys: a typo is a
//  compile error, and the full set of strings is enumerable.
//============================================================================
#include <string>

namespace toon {

    // The complete set of player-facing strings. Adding one here + a default in strings.cpp is the
    // whole cost of a new localizable string. Keep the two in the same order (Text indexes by this).
    enum class StrId {
        TitleHeading,    // the title-screen game name
        MenuNewGame,     //
        MenuContinue,    //
        MenuQuit,        //
        PausedHeading,   // the pause-menu heading
        MenuResume,      //
        MenuQuitToTitle, //
        Loading,         // the loading-screen label
        HudTimeLabel,    // HUD playtime label (the numeric value is formatted in code)
        HudPauseHint,    // HUD "how to pause" hint
        Count            // not a string: the table size
    };

    // The current-language string for `id` (English until LoadStrings swaps the table). Returns a
    // stable reference into the table, valid for the rest of the process.
    const std::string &Text(StrId id);

    // The stable data-file key for `id` (e.g. "menu.new_game"), independent of the display text --
    // what a per-language file addresses, so translations don't break when English wording changes.
    const char *KeyName(StrId id);

    // Roadmap #32 hook: override the table from a `key = value` UTF-8 text file (one per line, '#'
    // comments, keys per KeyName). Unknown keys are ignored; missing keys keep their English
    // default. Returns false if the file can't be opened. Not called yet (English is the default).
    bool LoadStrings(const char *path);

} // namespace toon
