#pragma once
//============================================================================
//  core/audio/audio.h: ToonEngine's audio seam.
//
//  Twin to core/physics/physics.h: the ONE header the rest of the engine talks to for
//  sound. Every miniaudio type (ma_engine, ma_sound, ...) lives behind it in audio.cpp via
//  PIMPL, so no miniaudio type (and no miniaudio header) escapes into engine or game
//  code (per the project's build-on-Diligent guiding principle and the physics/renderer
//  seams it already established). This header speaks only toon:: types: Vec3 (core/math.h)
//  and plain
//  enums/structs defined below.
//============================================================================
#include "core/math.h" // toon::Vec3 (plain, miniaudio- and Diligent-free)

#include <cstdint>
#include <string>

namespace toon {

    // --- Opaque handle -----------------------------------------------------------
    // One persistent (handled) sound, named the same opaque-32-bit-id way physics.h names a
    // body (BodyHandle) and renderer.h names GPU resources. The id-to-ma_sound mapping lives
    // entirely inside AudioEngine::Impl; nothing outside audio.cpp ever sees an ma_sound.
    enum class SoundHandle : uint32_t { Invalid = 0 };

    // --- Scene vocabulary ---------------------------------------------------------

    // Everything Play needs to start one persistent (handled) sound: a scene emitter
    // (app/audio_glue.cpp's BuildAudioWorld) or a music track. `clip` is typically just a
    // filename (e.g. "demo_hum.wav"). audio.cpp resolves anything that isn't already an
    // absolute path against TOON_AUDIO_DIR, so a full path also works if one is given.
    struct SoundDesc {
        std::string clip;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool spatial = true; // false = plays the same everywhere (music, UI); true = 3D positioned
        bool stream =
            false; // true = decode from disk as it plays (long music tracks); false = load fully upfront (short SFX)
        float maxDistance = 25.0f; // spatial only: beyond this, attenuation bottoms out
        Vec3 position;             // spatial only: initial world position (SetPosition updates it per frame)
    };

    // --- AudioEngine --------------------------------------------------------------
    // PIMPL, the same rationale as Renderer/PhysicsWorld: miniaudio already provides its own
    // internal dispatch (and owns its own realtime audio callback thread, see audio.cpp's
    // banner), so a second dispatch layer here would buy nothing.
    class AudioEngine {
    public:
        AudioEngine();
        ~AudioEngine();

        AudioEngine(const AudioEngine &) = delete;
        AudioEngine &operator=(const AudioEngine &) = delete;

        // One-time setup: miniaudio's engine + its device + realtime audio thread. Call once
        // (after the window/renderer exist, alongside PhysicsWorld::Init); Shutdown once at exit.
        bool Init();
        void Shutdown();

        // Listener: the "virtual ears" spatial sounds are heard relative to. Driven from the
        // editor camera every RENDERED frame (not the fixed sim tick, see docs/architecture.md's
        // audio section): audio is a presentation concern, like rendering, so it should track the
        // same smoothly-interpolated transform the camera renders with, not the stepped sim pose.
        void SetListener(const Vec3 &position, const Vec3 &forward, const Vec3 &up);

        // Fire-and-forget one-shots: play once, then miniaudio releases the resources itself.
        // There is no handle to hold or clean up. PlayOneShot is non-spatial (UI blips, global
        // SFX); PlayOneShotAt places a transient emitter at a world point (an impact, a pickup)
        // that plays once and is gone, mirroring Unity's PlayClipAtPoint.
        void PlayOneShot(const char *clip, float volume = 1.0f);
        void PlayOneShotAt(const char *clip, const Vec3 &position, float volume = 1.0f);

        // Persistent (handled) sounds: scene emitters (looping ambience, one per AudioSource
        // component) and music. Returns SoundHandle::Invalid on failure (bad path, logs to
        // stderr). Play starts it immediately (respecting SoundDesc::loop).
        SoundHandle Play(const SoundDesc &desc);
        void Stop(SoundHandle sound);                              // stop + release this one handled sound
        void SetPosition(SoundHandle sound, const Vec3 &position); // spatial handles only; per-frame
        void SetVolume(SoundHandle sound, float volume);

        // Play/Pause/Stop session control (app/audio_glue.h's BuildAudioWorld builds the set;
        // these three mirror PhysicsWorld's Clear() / the Playback panel's own Pause handling).
        void PauseAll();  // freeze every handled sound in place (Playing -> Paused)
        void ResumeAll(); // ... and continue it (Paused -> Playing)
        void StopAll();   // stop AND release every handled sound (Play session ends / Stop pressed)

        // Note on editor auditioning: there is no separate "preview" API. The Properties
        // panel's "Preview"/"Stop Preview" button (ui/panels/properties_panel.cpp) auditions an
        // AudioSource exactly as authored (loop, volume, pitch, spatial) by calling Play()
        // with a SoundDesc built from the component's own fields and holding the returned
        // handle until the button is pressed again (or another entity is previewed), then
        // calling Stop() on it. This works in any Editing/Playing/Paused mode since AudioEngine
        // itself has no notion of that state.

        void SetMasterVolume(float volume); // Settings panel's mute/volume control

    private:
        struct Impl; // defined in audio.cpp: hides all miniaudio types
        Impl *m_impl = nullptr;
    };

} // namespace toon
