//============================================================================
//  ui/panels/playback_panel.cpp: see playback_panel.h.
//============================================================================
#include "ui/panels/playback_panel.h"

#include "app/audio_glue.h"
#include "app/editor_state.h"
#include "app/physics_glue.h"

#include "IconsFontAwesome6.h"

namespace toon {

    void DrawPlaybackPanel(EditorState &state) {
        // playbackOpen captures the pre-Begin value: Begin(..., &state.showPlayback) may flip
        // showPlayback to false itself (the window's own close button), but Begin was still
        // CALLED this frame whenever we entered here, so End() below must still be paired
        // against that, not against showPlayback's possibly-just-changed value.
        const bool playbackOpen = state.showPlayback;
        if (playbackOpen && ImGui::Begin("Playback", &state.showPlayback,
                                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            // Row 1: Play/Pause, Step, Stop -- same fixed width (sized off "Pause", the longest
            // label) so the group centers cleanly and reads as one toolbar instead of 3 buttons
            // each hugging their own label.
            const float btnW =
                ImGui::CalcTextSize(ICON_FA_FORWARD_STEP).x + ImGui::GetStyle().FramePadding.x * 4.0f + 8.0f;
            const float rowWidth = btnW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - rowWidth) * 0.5f);

            // A lingering Properties-panel audio Preview (see properties_panel.cpp) shouldn't
            // keep sounding underneath real gameplay audio once a Play session starts, or
            // linger after Stop -- silence it at every transition below.
            auto stopPreview = [&state]() {
                if (state.previewHandle != SoundHandle::Invalid) {
                    state.runtime.audio.Stop(state.previewHandle);
                    state.previewHandle = SoundHandle::Invalid;
                    state.previewEntityIdx = -1;
                }
            };

            const bool isPlaying = (state.mode == EditorMode::Playing);
            if (ImGui::Button(isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(btnW, btnW))) {
                if (state.mode == EditorMode::Editing) {
                    stopPreview();
                    state.sceneBackup = state.runtime.scene; // snapshot: Stop restores exactly this
                    state.mode = EditorMode::Playing;
                    state.runtime.accumulator = 0.0;
                    CreateScripts(state.runtime.scene); // fire OnCreate once, entering this Play session
                    BuildPhysicsWorld(state.runtime.physicsWorld, state.runtime.scene,
                                      state.runtime.bodyToEntity);     // seed bodies from collider-bearing entities
                    BuildAudioWorld(state.runtime.audio, state.runtime.scene); // start autoplay emitters
                } else if (state.mode == EditorMode::Playing) {
                    state.mode = EditorMode::Paused;
                    state.runtime.audio.PauseAll();
                } else { // Paused -> resume
                    state.mode = EditorMode::Playing;
                    state.runtime.audio.ResumeAll();
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(isPlaying); // stepping while already continuously ticking isn't meaningful
            if (ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(btnW, btnW))) {
                if (state.mode == EditorMode::Editing) {
                    stopPreview();
                    state.sceneBackup = state.runtime.scene;
                    state.mode = EditorMode::Paused; // step lands paused, not playing
                    state.runtime.accumulator = 0.0;
                    CreateScripts(state.runtime.scene); // fire OnCreate once, entering this Play session
                    BuildPhysicsWorld(state.runtime.physicsWorld, state.runtime.scene,
                                      state.runtime.bodyToEntity);     // seed bodies from collider-bearing entities
                    BuildAudioWorld(state.runtime.audio, state.runtime.scene); // start autoplay emitters
                    state.runtime.audio.PauseAll();                    // step lands paused -- freeze right after starting
                }
                state.stepRequested = true;
                state.suppressNextFrameHistory = true; // one tick's worth of pose jump, not smooth motion
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(state.mode == EditorMode::Editing); // nothing to stop yet
            if (ImGui::Button(ICON_FA_STOP, ImVec2(btnW, btnW))) {
                stopPreview();
                state.runtime.physicsWorld.Clear();      // release this session's bodies before the scene reverts
                state.runtime.bodyToEntity.clear();      // this session's contact-event lookup, same reason
                state.runtime.audio.StopAll();           // release this session's sounds before the scene reverts
                state.runtime.scene = state.sceneBackup; // discard everything Play did -- see the panel comment above
                state.mode = EditorMode::Editing;
                state.runtime.accumulator = 0.0;
                state.suppressNextFrameHistory = true;
            }
            ImGui::EndDisabled();
        }
        if (playbackOpen) { ImGui::End(); }
    }

} // namespace toon
