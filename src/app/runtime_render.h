#pragma once
//============================================================================
//  app/runtime_render.h: the scene render, shared by the editor and the player.
//
//  Everything a shipped game draws: the cascaded shadow pre-pass, the sky backdrop, the
//  opaque toon pass, the transparent sprite pass, and the DiligentFX post resolve. Takes a
//  RuntimeState&, so the player draws a frame with no editor overlays at all. The editor's
//  RenderFrame (app/editor_render.h) calls this, then adds its authoring-only trailers (grid,
//  collider wireframes, mouse-pick markers) on top.
//============================================================================
#include "app/runtime_state.h"

namespace toon {

    // Draw the scene into the back buffer: shadow pass -> BeginFrame -> sky -> opaque -> sprites
    // -> EndScene (the DiligentFX post chain + tone-map resolve). Assumes camera/post/light were
    // already pushed to the renderer this frame (TickRuntime does that). No ImGui, no overlays.
    void RenderScene(RuntimeState &rs);

} // namespace toon
