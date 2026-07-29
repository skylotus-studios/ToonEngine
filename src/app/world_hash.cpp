//============================================================================
//  app/world_hash.cpp: see world_hash.h.
//============================================================================
#include "app/world_hash.h"

#include "app/runtime_state.h"

#include <cstring>
#include <sstream>
#include <string>

namespace toon {
    namespace {

        // --- FNV-1a, 64-bit ---------------------------------------------------------
        // Chosen for exactly this job: a fast, fully deterministic (across identical builds),
        // non-cryptographic hash with a trivial byte-at-a-time incremental form -- no library, no
        // new dependency, and none of std::hash's guarantee gap (the standard explicitly leaves
        // std::hash's algorithm unspecified and libstdc++/MSVC versions have changed it, which
        // would make "identical across two runs of possibly-different builds" luck rather than a
        // guarantee).
        constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
        constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

        struct Fnv1a {
            uint64_t value = kFnvOffsetBasis;

            void AddBytes(const void *data, size_t size) {
                const auto *bytes = static_cast<const uint8_t *>(data);
                for (size_t i = 0; i < size; ++i) {
                    value ^= bytes[i];
                    value *= kFnvPrime;
                }
            }

            // Every overload below funnels through AddBytes on the type's own object
            // representation: fine for this use (a same-process, same-build determinism check,
            // never persisted or compared across machines/compilers), unlike a save file or a
            // network protocol, which would need an explicit wire format.
            void Add(const Vec3 &v) { AddBytes(&v, sizeof(v)); }
            void Add(const Quat &q) { AddBytes(&q, sizeof(q)); }
            void Add(float f) { AddBytes(&f, sizeof(f)); }
            void Add(int32_t i) { AddBytes(&i, sizeof(i)); }
            void Add(uint32_t u) { AddBytes(&u, sizeof(u)); }
            void Add(unsigned long long u) { AddBytes(&u, sizeof(u)); }
            void Add(const std::string &s) { AddBytes(s.data(), s.size()); }
        };

    } // namespace

    uint64_t HashWorldState(const RuntimeState &rs, unsigned long long seed) {
        Fnv1a h;
        // Domain separator: folds the seed into the hash's starting state (NOT into anything
        // simulated -- see this file's header comment on what that does and doesn't prove).
        h.Add(seed);

        for (size_t i = 0; i < rs.scene.entities.size(); ++i) {
            const Entity &e = rs.scene.entities[i];

            // The entity's id: its vector index, the same identity convention Entity::parent,
            // Scene::selected, and ContactEvent's `other` already use (see scene.h) -- there is
            // no separate GUID to hash instead. `parent` is included too, so a structural
            // difference (the same entities in a different hierarchy shape) is caught even if
            // every entity's own data happens to match.
            h.Add(static_cast<uint32_t>(i));
            h.Add(e.parent);

            // Local pose, current tick -- NOT e.worldMatrix. See world_hash.h's banner: with
            // TickRuntime's render-interpolation alpha always landing on exactly 0 in --sim-only
            // (every tick is exactly kFixedDt), worldMatrix reflects the PREVIOUS tick's pose.
            // transform is the just-computed authoritative one.
            if (e.transform) {
                h.Add(e.transform->position);
                h.Add(e.transform->rotation);
                h.Add(e.transform->scale);
            }

            // The physics body's own state, straight from Jolt -- not e.transform's mirror of it
            // (which BuildPhysicsWorld/TickRuntime only writes back for a Dynamic body, and only
            // AFTER Step). Catches a solver-level divergence even before anything reads it back.
            if (e.body && e.body->handle != BodyHandle::Invalid) {
                Vec3 bodyPos, linear, angular;
                Quat bodyRot;
                if (rs.physicsWorld.GetBodyTransform(e.body->handle, bodyPos, bodyRot)) {
                    h.Add(bodyPos);
                    h.Add(bodyRot);
                }
                if (rs.physicsWorld.GetBodyVelocity(e.body->handle, linear, angular)) {
                    h.Add(linear);
                    h.Add(angular);
                }
            }

            // Script-visible state: each script's own Save() -- the exact serialization it
            // already defines for itself (script.h), reused here instead of a new virtual. No
            // engine-side knowledge of which Script subclass this is.
            for (const ScriptComponent &sc : e.scripts) {
                if (!sc.instance) { continue; }
                std::ostringstream out;
                sc.instance->Save(out);
                h.Add(out.str());
            }

            // Animation: clip + time, exactly what the task asks for. prevTime is last tick's
            // value (derived, not current state) and deliberately excluded.
            if (e.animation) {
                h.Add(e.animation->clipIndex);
                h.Add(e.animation->time);
            }
        }

        return h.value;
    }

    uint64_t HashScriptOrder(const RuntimeState &rs) {
        Fnv1a h;
        for (size_t i = 0; i < rs.scene.entities.size(); ++i) {
            const Entity &e = rs.scene.entities[i];
            for (const ScriptComponent &sc : e.scripts) {
                if (!sc.instance) { continue; }
                h.Add(static_cast<uint32_t>(i));
                h.Add(sc.name);
            }
        }
        return h.value;
    }

} // namespace toon
