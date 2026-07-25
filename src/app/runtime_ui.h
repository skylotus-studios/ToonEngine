#pragma once
//============================================================================
//  app/runtime_ui.h: the in-game HUD/menu driver (roadmap #17), shared by the player and the
//  editor's play-in-editor.
//
//  RenderHUD builds the current screen's UI through the Fleury box API (core/ui/ui.h) and draws
//  it over the resolved back buffer -- call it AFTER Renderer::EndScene, the same timing as
//  DrawGrid/DrawWireframe (see Renderer::DrawUI). On the player, menu actions drive
//  SetAppState / BeginNewGame / BeginContinue (app/app_state.h). Twin to app/runtime_render.h:
//  Diligent-free, reaching the GPU only through the renderer seam.
//============================================================================
#include "app/app_state.h" // AppState

namespace toon {

    struct RuntimeState;

    // Which screen the HUD draws this frame. The player maps its AppState via ScreenForAppState;
    // the editor passes Playing while play-in-editor runs (a passive readout) and None otherwise --
    // it never shows the Title/Pause menus, whose buttons drive SetAppState (which the editor
    // doesn't use: its own EditorMode owns play/pause).
    enum class UIScreen { None, Title, Loading, Playing, Paused };

    UIScreen ScreenForAppState(AppState state);

    // Build + draw `screen` for this frame. Reads raw mouse (scaled to framebuffer pixels) +
    // keyboard/gamepad nav from Input::. A no-op for UIScreen::None or before the font loads.
    void RenderHUD(RuntimeState &rs, UIScreen screen);

} // namespace toon
