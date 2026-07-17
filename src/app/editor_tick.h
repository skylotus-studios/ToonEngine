#pragma once
//============================================================================
//  app/editor_tick.h — per-frame input, fixed-timestep sim, and camera nav.
//============================================================================
namespace toon {

    struct EditorState;

    // Polls input, advances the fixed-timestep sim (scripts + physics) by however many whole
    // ticks the frame owes, updates the scene's interpolated world transforms, navigates the
    // editor camera, and pushes the resulting post-params/camera/light into state.renderer.
    // Call once per frame, before the shadow pass + main draw (RenderFrame, editor_render.h).
    void TickEditor(EditorState &state);

} // namespace toon
