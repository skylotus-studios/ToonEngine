#pragma once
//============================================================================
//  app/editor_tick.h: per-frame input, fixed-timestep sim, and camera nav.
//============================================================================
namespace toon {

    struct EditorState;

    // Polls input, advances the fixed-timestep sim (scripts + physics) by however many whole
    // ticks the frame owes, updates the scene's interpolated world transforms, navigates the
    // editor camera, and pushes the resulting post-params/camera/light into state.renderer.
    // Call once per frame, before the shadow pass + main draw (RenderFrame, editor_render.h).
    void TickEditor(EditorState &state);

    // 2D editor mode (roadmap #14): entering (on2D = true) saves the current 3D yaw/pitch
    // (EditorState::saved3DYaw/Pitch) and snaps the camera to a fixed angle facing the sprite
    // plane, then sets Camera::orthographic; leaving restores the saved angle and clears it.
    // Editor policy (what "2D mode" means for this app), not camera math, so it lives here
    // rather than in core/camera -- the Settings panel checkbox calls this instead of writing
    // camera.orthographic directly, so the angle save/restore always happens with the flag.
    void SetEditorMode2D(EditorState &state, bool on2D);

} // namespace toon
