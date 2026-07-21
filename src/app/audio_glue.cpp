//============================================================================
//  app/audio_glue.cpp: see audio_glue.h.
//============================================================================
#include "app/audio_glue.h"

namespace toon {

    void BuildAudioWorld(AudioEngine &audio, Scene &scene) {
        for (Entity &e : scene.entities) {
            if (!e.audioSource || !e.audioSource->autoplay) { continue; }

            SoundDesc desc;
            desc.clip = e.audioSource->clip;
            desc.volume = e.audioSource->volume;
            desc.pitch = e.audioSource->pitch;
            desc.loop = e.audioSource->loop;
            desc.spatial = e.audioSource->spatial;
            desc.stream = e.audioSource->stream;
            desc.maxDistance = e.audioSource->maxDistance;
            // World position, straight from the cached matrix (row-major, translation in
            // m[12..14], see core/math.h's Mat4 banner). Exact for a root-parented entity;
            // see this file's own header comment for the nested-entity simplification.
            desc.position = {e.worldMatrix.m[12], e.worldMatrix.m[13], e.worldMatrix.m[14]};

            e.audioSource->handle = audio.Play(desc);
        }
    }

} // namespace toon
