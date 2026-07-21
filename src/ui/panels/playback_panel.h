#pragma once
//============================================================================
//  ui/panels/playback_panel.h: Play / Pause / Step / Stop for the fixed-timestep sim.
//============================================================================
namespace toon {

    struct EditorState;

    // Docked as a thin top strip (see dockspace.cpp), with its dock node's tab bar suppressed
    // so it reads as a fixed toolbar, not a document with a title. Editing (default): nothing
    // simulates. Playing: the M1.1 accumulator runs (editor_tick.cpp). Paused: frozen mid-play,
    // scene stays put. Stop always restores state.sceneBackup -- Play is a disposable sandbox,
    // never a permanent edit, which is what makes testing gameplay/physics safe to experiment
    // with.
    void DrawPlaybackPanel(EditorState &state);

} // namespace toon
