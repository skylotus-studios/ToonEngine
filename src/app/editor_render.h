#pragma once
//============================================================================
//  app/editor_render.h — shadow pass, main draw, and the collider debug overlay.
//============================================================================
namespace toon {

    struct EditorState;

    // Renders the shadow-cascade pre-pass, the main scene (every renderable entity, styled by
    // state.style/outlineScale), resolves to the back buffer (Renderer::EndScene), then draws
    // the collider wireframe overlay if state.showColliders is set. Call once per frame, after
    // TickEditor (editor_tick.h) and before Renderer::BeginUI.
    void RenderFrame(EditorState &state);

} // namespace toon
