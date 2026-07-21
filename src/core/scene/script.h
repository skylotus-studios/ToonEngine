#pragma once
//============================================================================
//  core/scene/script.h: native gameplay scripts, per-entity Update hooks.
//
//  The M1.3 entity behavior system. An Entity carries zero or more ScriptComponents;
//  each wraps a heap-owned Script subclass (Cherno/Hazel's NativeScriptComponent
//  shape) reached by a name-keyed registry, so a saved scene (or an in-memory Entity
//  copy, see scene.h's Entity copy constructor) can reconstruct the right subclass
//  from just that name. ECS (entt et al.) is deliberately deferred until entity count or a
//  profiled hotspot
//  actually demands it; this stays a thin, Diligent-free layer over the existing
//  entity vector.
//============================================================================
#include "core/math.h" // Vec3 -- OnCollision*'s point/normal params

#include <functional>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace toon {

    struct Entity;
    struct Scene;

    // A native (C++) gameplay script attached to an entity via ScriptComponent. Holds NO
    // private simulation state of its own: anything that needs to persist or be
    // snapshotted (Save/Load, Entity's copy constructor) lives as data on the Entity
    // (or a future component); a script only ever reads/writes that data through `self`.
    // This keeps a script a pure function of (entity data, dt), so a future fast state
    // snapshot (e.g. for rollback netcode) never needs to know anything about which
    // scripts exist; see the M1.3 planning session's "Forward-compatibility check".
    class Script {
    public:
        virtual ~Script() = default;

        virtual void OnCreate(Entity &self, Scene &scene) {}
        virtual void OnUpdate(Entity &self, Scene &scene, float dt) {}
        // Declared for API symmetry (matches Unity/Hazel naming); not wired to fire in
        // M1.3: there's no mid-Play entity spawn/destroy yet for it to signal.
        virtual void OnDestroy(Entity &self, Scene &scene) {}

        // Physics contact hooks (roadmap #9), driven by a Jolt ContactListener
        // (core/physics/physics.h's ContactEvent) via app/physics_glue.h's
        // DispatchContactEvents. `other` is an entity INDEX, not a reference -- matching
        // Entity::parent/Scene::selected/ReparentEntity's own idx convention, never a raw
        // Entity& alias into the entities vector (see Scene's own "never call [structural
        // mutators] mid-iteration" caution). `point`/`normal` are world-space; `normal` points
        // away from `self`, toward `other` (Unity's own convention) -- DispatchContactEvents
        // flips physics.h's a->b normal as needed per side. Enter fires once when contact
        // starts, Stay every tick it continues, Exit once it ends (Exit's point/normal are the
        // last ones seen, not live geometry -- see ContactEvent's own comment for why).
        virtual void OnCollisionEnter(Entity &self, Scene &scene, int other, const Vec3 &point, const Vec3 &normal) {}
        virtual void OnCollisionStay(Entity &self, Scene &scene, int other, const Vec3 &point, const Vec3 &normal) {}
        virtual void OnCollisionExit(Entity &self, Scene &scene, int other, const Vec3 &point, const Vec3 &normal) {}

        // Read/write THIS script's own fields only (never the entity's transform, which
        // the entity-level serializer already owns), as space-separated tokens appended
        // to the current line; see serializer.cpp's "script <Name> ..." line format.
        // Also the mechanism Entity's copy constructor uses to deep-clone a script.
        virtual void Save(std::ostream &out) const {}
        virtual void Load(std::istream &in) {}
    };

    using ScriptFactory = std::function<std::unique_ptr<Script>()>;

    // Register a native script type under a stable name, so a saved scene or an
    // in-memory Entity clone can reconstruct the right subclass from just that name.
    // Call once per script type; see core/scene/scripts/spin_script.cpp's self-registering
    // static for the pattern every future script follows.
    void RegisterScript(const std::string &name, ScriptFactory factory);

    // Construct a script instance by its registered name. Returns nullptr (and logs to
    // stderr) if the name isn't registered, e.g. a scene saved by a build with a script
    // this build doesn't have.
    std::unique_ptr<Script> CreateScript(const std::string &name);

    // List every registered script type name, alphabetically, for an editor "Add Script"
    // picker (main.cpp's Inspector). Registration happens via each script's self-registering
    // static before main() ever runs, so this list is fixed for the process's whole lifetime.
    std::vector<std::string> GetRegisteredScriptNames();

    // One entity's attached script: a stable name (for the registry + save file) plus
    // the live instance. A vector, not a single optional, on Entity: an entity
    // plausibly carries more than one independent concern (e.g. a Health script
    // alongside a PlayerMovement script).
    struct ScriptComponent {
        std::string name;
        std::unique_ptr<Script> instance;
    };

    // Call once, in entity order, when a Play session begins (see main.cpp's Playback
    // panel), before any OnUpdate this session.
    void CreateScripts(Scene &scene);

    // Call once per fixed sim tick (see main.cpp's frame loop), after SnapshotSimState
    // and before UpdateWorldTransforms.
    void UpdateScripts(Scene &scene, float dt);

} // namespace toon
