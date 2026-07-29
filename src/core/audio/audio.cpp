//============================================================================
//  core/audio/audio.cpp: miniaudio backend behind the audio seam.
//
//  The one translation unit allowed to include miniaudio.h or name an ma_* type: twin to
//  core/physics/physics.cpp's relationship with Jolt (see core/audio/audio.h's banner).
//  miniaudio.h's actual ~90k-line implementation is compiled once in the sibling
//  miniaudio_impl.cpp, not here, so editing this (much smaller, much more frequently
//  touched) file never re-pays that compile cost.
//
//  miniaudio's ma_engine owns its OWN realtime audio callback thread (see miniaudio.h's
//  "5. High Level API" docs). Nothing in this file, or the frame loop that calls it, ever
//  touches the mixing itself. The engine's own thread pulls PCM frames on its own schedule;
//  this file's job is only to (a) start/stop sounds and (b) push position updates the
//  engine's thread will pick up on its next mix. Every position/volume/pitch setter below
//  (ma_sound_set_*, ma_engine_listener_set_*) is designed to be called from the caller's
//  thread while the audio thread reads it concurrently (see miniaudio.h's own docs); this
//  file must never call INTO miniaudio from an ma_sound_end_callback (that callback fires
//  ON the audio thread, and miniaudio's own docs say NOT to uninitialize a sound from it),
//  which is why one-shots below use a lazily-recycled voice pool instead, matching the exact
//  technique ma_engine_play_sound_ex uses internally for its own "inlined sounds".
//
//  World-space vectors (entity positions, the listener's eye/forward/up from
//  CameraWorldBasis) are handed to miniaudio as-is: ToonEngine's world space is already
//  self-consistent (every position and direction below comes from the same scene/camera
//  math), and miniaudio only ever compares vectors against each other in that same space.
//  It has no opinion on world handedness, so no basis conversion is needed here.
//============================================================================
#include "core/audio/audio.h"

#include "core/platform/paths.h" // Assets::Audio (exe-relative audio clip dir)

#include "miniaudio.h"

#include <cctype>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>

namespace toon {

    namespace {

        // A one-shot voice slot, recycled once its sound finishes (mirrors miniaudio's own
        // ma_engine_play_sound_ex recycling of "inlined sounds", see this file's banner).
        // Heap-allocated (one `new` per slot, never moved) so `sound`'s address is stable for
        // its whole lifetime; miniaudio's node graph keeps pointers into it once attached.
        struct OneShotVoice {
            ma_sound sound{};
            bool live = false; // true once ma_sound_init_from_file has succeeded on `sound`
        };

        bool IsAbsoluteClipPath(const std::string &path) {
            if (path.empty()) { return false; }
            if (path[0] == '/' || path[0] == '\\') { return true; } // POSIX root, or a UNC/rooted Windows path
            // "C:/..." / "C:\..." -- a drive letter followed by a separator.
            return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
        }

        // AudioSource::clip (Entity component field, and the Properties panel's Clip text
        // box) is meant to be typed as just a filename, e.g. "demo_hum.wav" -- same "type the
        // asset's name, not a path" expectation the Asset Browser sets everywhere else in the
        // editor. Resolve it against the audio/ asset dir unless it already looks like a full
        // path (so a scene file that stores an absolute path, or a future asset-browser picker,
        // still works unchanged).
        std::string ResolveClipPath(const std::string &clip) {
            if (IsAbsoluteClipPath(clip)) { return clip; }
            return Assets::Audio() + "/" + clip;
        }

    } // namespace

    struct AudioEngine::Impl {
        ma_engine engine{};
        bool initialized = false;

        // SoundHandle (this seam's opaque id) -> the real ma_sound it names. Stored by VALUE
        // (not by pointer). unordered_map guarantees a stored element's address never moves
        // for its lifetime (only erase invalidates it), so ma_sound_init_from_file can target
        // `handled[id]` directly and miniaudio's internal pointers into it stay valid.
        std::unordered_map<uint32_t, ma_sound> handled;
        uint32_t nextHandle = 1; // 0 is SoundHandle::Invalid

        std::vector<std::unique_ptr<OneShotVoice>> oneShotVoices;

        void PlayManagedOneShot(const char *clip, float volume, bool spatial, const Vec3 &position) {
            if (!initialized) { return; }

            // Reclaim the first finished voice, exactly like miniaudio's own inlined-sound
            // pool: scan for one at its end, uninit it in place, and reuse the slot. Only
            // allocate a new one if every existing voice is still playing.
            OneShotVoice *voice = nullptr;
            for (auto &v : oneShotVoices) {
                if (v->live && ma_sound_at_end(&v->sound)) {
                    ma_sound_uninit(&v->sound);
                    v->live = false;
                }
                if (!v->live) {
                    voice = v.get();
                    break;
                }
            }
            if (!voice) {
                oneShotVoices.push_back(std::make_unique<OneShotVoice>());
                voice = oneShotVoices.back().get();
            }

            // DECODE: pay the decode cost once upfront instead of per-mix (these are short
            // clips). NO_SPATIALIZATION for the non-positional case (see MA_SOUND_FLAG_*'s
            // own "16.2 High Level API" optimization tip).
            ma_uint32 flags = MA_SOUND_FLAG_DECODE;
            if (!spatial) { flags |= MA_SOUND_FLAG_NO_SPATIALIZATION; }
            const std::string resolvedClip = ResolveClipPath(clip);
            if (ma_sound_init_from_file(&engine, resolvedClip.c_str(), flags, nullptr, nullptr, &voice->sound) !=
                MA_SUCCESS) {
                std::fprintf(stderr, "AudioEngine: failed to load '%s'\n", resolvedClip.c_str());
                return;
            }
            voice->live = true;
            ma_sound_set_volume(&voice->sound, volume);
            if (spatial) { ma_sound_set_position(&voice->sound, position.x, position.y, position.z); }
            ma_sound_start(&voice->sound);
        }
    };

    AudioEngine::AudioEngine() : m_impl(new Impl()) {}

    AudioEngine::~AudioEngine() { delete m_impl; }

    bool AudioEngine::Init() {
        const ma_engine_config config = ma_engine_config_init();
        if (ma_engine_init(&config, &m_impl->engine) != MA_SUCCESS) {
            std::fprintf(stderr, "AudioEngine: ma_engine_init failed\n");
            return false;
        }
        m_impl->initialized = true;
        return true;
    }

    void AudioEngine::Shutdown() {
        if (!m_impl->initialized) { return; }

        for (auto &[handle, sound] : m_impl->handled) {
            ma_sound_uninit(&sound);
        }
        m_impl->handled.clear();

        for (auto &voice : m_impl->oneShotVoices) {
            if (voice->live) { ma_sound_uninit(&voice->sound); }
        }
        m_impl->oneShotVoices.clear();

        ma_engine_uninit(&m_impl->engine);
        m_impl->initialized = false;
    }

    void AudioEngine::SetListener(const Vec3 &position, const Vec3 &forward, const Vec3 &up) {
        if (!m_impl->initialized) { return; }
        constexpr ma_uint32 kListenerIndex = 0; // a single editor camera/listener, no split-screen
        ma_engine_listener_set_position(&m_impl->engine, kListenerIndex, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&m_impl->engine, kListenerIndex, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&m_impl->engine, kListenerIndex, up.x, up.y, up.z);
    }

    void AudioEngine::PlayOneShot(const char *clip, float volume) {
        m_impl->PlayManagedOneShot(clip, volume, false, Vec3{});
    }

    void AudioEngine::PlayOneShotAt(const char *clip, const Vec3 &position, float volume) {
        m_impl->PlayManagedOneShot(clip, volume, true, position);
    }

    SoundHandle AudioEngine::Play(const SoundDesc &desc) {
        if (!m_impl->initialized) { return SoundHandle::Invalid; }

        ma_uint32 flags = desc.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        if (!desc.spatial) { flags |= MA_SOUND_FLAG_NO_SPATIALIZATION; }

        const uint32_t id = m_impl->nextHandle++;
        // operator[] value-initializes a fresh ma_sound IN PLACE at its final map-node
        // address (no later move/copy) -- see Impl::handled's comment for why that matters.
        ma_sound &sound = m_impl->handled[id];
        const std::string resolvedClip = ResolveClipPath(desc.clip);
        if (ma_sound_init_from_file(&m_impl->engine, resolvedClip.c_str(), flags, nullptr, nullptr, &sound) !=
            MA_SUCCESS) {
            std::fprintf(stderr, "AudioEngine: failed to load '%s'\n", resolvedClip.c_str());
            m_impl->handled.erase(id);
            return SoundHandle::Invalid;
        }

        ma_sound_set_volume(&sound, desc.volume);
        ma_sound_set_pitch(&sound, desc.pitch);
        ma_sound_set_looping(&sound, desc.loop ? MA_TRUE : MA_FALSE);
        if (desc.spatial) {
            ma_sound_set_position(&sound, desc.position.x, desc.position.y, desc.position.z);
            ma_sound_set_max_distance(&sound, desc.maxDistance);
        }
        ma_sound_start(&sound);

        return static_cast<SoundHandle>(id);
    }

    void AudioEngine::Stop(SoundHandle sound) {
        const auto it = m_impl->handled.find(static_cast<uint32_t>(sound));
        if (it == m_impl->handled.end()) { return; }
        ma_sound_uninit(&it->second);
        m_impl->handled.erase(it);
    }

    void AudioEngine::SetPosition(SoundHandle sound, const Vec3 &position) {
        const auto it = m_impl->handled.find(static_cast<uint32_t>(sound));
        if (it == m_impl->handled.end()) { return; }
        ma_sound_set_position(&it->second, position.x, position.y, position.z);
    }

    void AudioEngine::SetVolume(SoundHandle sound, float volume) {
        const auto it = m_impl->handled.find(static_cast<uint32_t>(sound));
        if (it == m_impl->handled.end()) { return; }
        ma_sound_set_volume(&it->second, volume);
    }

    void AudioEngine::PauseAll() {
        for (auto &[handle, sound] : m_impl->handled) {
            ma_sound_stop(&sound);
        }
    }

    void AudioEngine::ResumeAll() {
        for (auto &[handle, sound] : m_impl->handled) {
            ma_sound_start(&sound);
        }
    }

    void AudioEngine::StopAll() {
        for (auto &[handle, sound] : m_impl->handled) {
            ma_sound_uninit(&sound);
        }
        m_impl->handled.clear();
    }

    void AudioEngine::SetMasterVolume(float volume) {
        if (!m_impl->initialized) { return; }
        ma_engine_set_volume(&m_impl->engine, volume);
    }

    size_t AudioEngine::ActiveVoiceCount() const { return m_impl->handled.size(); }

} // namespace toon
