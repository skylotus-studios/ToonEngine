#pragma once
//============================================================================
//  core/scene/scene.h — the scene graph: an entity tree with hierarchy-composed world
//  transforms.
//
//  Backend-agnostic like the rest of the seam vocabulary (toon::Transform + Mat4 +
//  opaque handles) — no Diligent types leak here. The world-matrix composition (the
//  actual 4x4 math) lives in scene.cpp, which IS allowed to use Diligent
//  (build-on-Diligent — see CLAUDE.md). Adapted from ToonEngineOld/src/scene.
//============================================================================
#include "core/rendering/renderer.h"   // Transform, Material, Mesh/ModelHandle, Mat4 (via math.h)
#include "core/rendering/primitives.h" // PrimitiveDesc — mesh-regeneration params for serialization
#include "core/physics/physics.h"      // ColliderShape/BodyType/BodyHandle — Entity's physics components (M2.1)
#include "core/audio/audio.h"          // SoundHandle — Entity's AudioSource component (M2.2)
#include "core/scene/script.h"         // ScriptComponent — Entity's attached native scripts (M1.3)

#include <optional>
#include <string>
#include <vector>

namespace toon {

    // A directional light carried by an entity. Aimed by the entity's ROTATION (its local +Z
    // axis in world space is the direction light rays travel — Unity/Godot-style; rotate the
    // entity, e.g. with the gizmo, to re-aim it). See MakeLightTransform / GetActiveLight.
    struct LightComponent {
        Vec3 color = {1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
    };

    // A collision shape carried by an entity (M2.1) — the physics seam's ColliderShape/Vec3
    // vocabulary (core/physics/physics.h). An entity with a collider but no RigidBodyComponent is an
    // implicit static collider (a wall/floor); paired with one, it becomes a dynamic/kinematic
    // mover — see RigidBodyComponent below.
    struct ColliderComponent {
        ColliderShape shape = ColliderShape::Box;
        Vec3 extents = {0.5f, 0.5f, 0.5f}; // meaning depends on shape — see BodyDesc
    };

    // Physics behavior for an entity that also carries a ColliderComponent (M2.1). `handle` is
    // runtime-only state (like Entity::worldMatrix) — populated when the physics world is built
    // for a Play session (main.cpp) and never serialized; it copies as a plain id, which is
    // harmless since a copy is only ever a snapshot/backup (Play/Stop, DuplicateEntity) and the
    // real body is rebuilt the next time Play starts.
    struct RigidBodyComponent {
        BodyType type = BodyType::Dynamic;
        float mass = 1.0f; // ignored for Static/Kinematic — see BodyDesc
        float friction = 0.5f;
        float restitution = 0.2f;
        BodyHandle handle = BodyHandle::Invalid;
    };

    // A sound emitter carried by an entity (M2.2) — the audio seam's SoundDesc vocabulary
    // (core/audio/audio.h). `clip` is a path under TOON_AUDIO_DIR, same "asset path on the
    // component, GPU/audio resource behind a handle" split as Entity::modelPath/model. Autoplay
    // emitters start when a Play/Step session begins (app/audio_glue.cpp's BuildAudioWorld);
    // `handle` is runtime-only state (like RigidBodyComponent::handle above) — populated then,
    // never serialized, harmless to copy as a plain id (Play/Stop, DuplicateEntity are snapshots;
    // the real sound is rebuilt the next time Play starts).
    struct AudioSource {
        std::string clip;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool autoplay = true;
        bool spatial = true;       // false = plays the same everywhere (ambience/music/UI)
        bool stream = false;       // true = decode from disk as it plays (long music tracks)
        float maxDistance = 25.0f; // spatial only — see SoundDesc
        SoundHandle handle = SoundHandle::Invalid;
    };

    // One node in the scene. It renders either a procedural primitive (`mesh` set) or a loaded
    // glTF model (`model` set); `material` is the primitive's material, or the model's
    // style/tint. `transform == nullopt` marks a pure grouping/anchor node (the root) that
    // simply passes its parent's world matrix through. A renderable and a light are mutually
    // exclusive in practice (nothing draws a light), but nothing enforces that here.
    struct Entity {
        std::string name;
        int parent = 0; // parent index; -1 marks the root (index 0 only)

        std::optional<Transform> transform = Transform{}; // local placement (current sim tick)
        // Previous sim-tick local pose. UpdateWorldTransforms interpolates transform between this
        // and `transform` (see its `alpha` param) so rendering stays smooth even when the render
        // rate doesn't match the fixed sim rate. nullopt = no prior sim state yet (a fresh/loaded
        // entity, or an anchor) -> no interpolation (renders straight `transform`).
        std::optional<Transform> prevSimTransform;
        Mat4 worldMatrix;     // cached: interpolated local * parent.world (UpdateWorldTransforms)
        Mat4 prevWorldMatrix; // last frame's world, for motion vectors

        MeshHandle mesh = MeshHandle::Invalid;    // a procedural primitive, or...
        ModelHandle model = ModelHandle::Invalid; // ...a loaded glTF model
        Material material;                        // primitive material / model tint + style

        // Provenance for `mesh`/`model`, so a saved scene can rebuild the GPU resource on load
        // (see core/scene/serializer.h): a procedural mesh has no source file, so `primitive.kind` +
        // params regenerate it via MakePrimitiveMesh; a loaded model does, so `modelPath` re-feeds
        // Renderer::LoadModel. Both stay empty/None on a non-renderable (grouping/light) entity.
        PrimitiveDesc primitive;
        std::string modelPath;

        std::optional<LightComponent> light; // set -> this entity is a (directional) light

        // Physics (M2.1): a collider alone is a static collider (see ColliderComponent above);
        // paired with a RigidBodyComponent, the entity becomes a dynamic/kinematic mover.
        std::optional<ColliderComponent> collider;
        std::optional<RigidBodyComponent> body;

        // Audio (M2.2): a positional or non-positional sound emitter — see AudioSource above.
        std::optional<AudioSource> audioSource;

        // Attached native scripts (core/scene/script.h) — per-tick gameplay hooks (M1.3). A
        // std::unique_ptr member makes ScriptComponent, and therefore Entity, NOT implicitly
        // copyable — the copy constructor/assignment below deep-clone each script via the
        // name registry (CreateScript) and its own Save/Load, instead of a raw memberwise
        // copy. This is what keeps main.cpp's Play/Stop (`sceneBackup = scene` / `scene =
        // sceneBackup`) and DuplicateEntity (scene.cpp) working unchanged: a copy is no
        // longer free, but it's still just a copy from the caller's point of view, and it
        // never touches the Renderer (mesh/model handles are copied as plain IDs), so it
        // can't leak or re-upload a GPU resource the way a full scene-file reload would.
        std::vector<ScriptComponent> scripts;

        Entity() = default;
        Entity(const Entity &other);
        Entity &operator=(const Entity &other);
        Entity(Entity &&) = default;            // cheap: moves the scripts vector's buffer,
        Entity &operator=(Entity &&) = default; // never touches an individual ScriptComponent
        ~Entity() = default;
    };

    // A scene is a flat vector with parents ALWAYS before their children, so a single forward
    // pass composes world transforms. Index 0 is the implicit root.
    struct Scene {
        std::vector<Entity> entities;
        int selected = -1; // index into entities; -1 = none (editor selection)
    };

    // Ensure a root exists at index 0 (parent = -1, no transform). Call once, before adding
    // entities; existing entities are shifted down and re-parented to the new root.
    void EnsureSceneRoot(Scene &scene);

    // Append a new entity under `parent` (defaults to the root on a bad index) and return its
    // index. Build the scene parent-first so the parents-before-children invariant holds.
    int AddEntity(Scene &scene, int parent, const char *name);

    // Recompute every entity's cached worldMatrix from the hierarchy (one forward pass, since
    // parents precede children), snapshotting the previous frame's matrices first. Each entity's
    // local pose is interpolated between prevSimTransform and transform by `alpha` (the fixed-
    // timestep accumulator's fraction into the next sim tick; see main.cpp's frame loop) — the
    // default 1.0 renders the current sim tick exactly, for callers outside that loop (e.g. right
    // after a scene load). Call once per rendered frame, before drawing.
    void UpdateWorldTransforms(Scene &scene, float alpha = 1.0f);

    // Copy every transformed entity's current `transform` into `prevSimTransform`. Call once at
    // the start of each FIXED sim step (before advancing any simulation state for that step), so
    // UpdateWorldTransforms has a correct interval to interpolate across afterward.
    void SnapshotSimState(Scene &scene);

    // Set entity `idx`'s transform from a WORLD matrix (the editor gizmo edits in world space):
    // folds out the parent's world and decomposes to the local TRS the renderer recomposes. No-op
    // on a transform-less anchor. Call between UpdateWorldTransforms passes (uses cached worlds).
    void SetEntityWorldMatrix(Scene &scene, int idx, const Mat4 &world);

    // Build a Transform whose rotation aims local +Z at `dirToLight` (Unity/Godot-style light
    // aiming) and whose position is `position`; scale is identity. Used to seed the scripted
    // default light. Call UpdateWorldTransforms afterward for it to take effect.
    Transform MakeLightTransform(const Vec3 &position, const Vec3 &dirToLight);

    // Compose a world Mat4 from a position + quaternion rotation + scale — lets a Diligent-free
    // caller (main.cpp) build a Mat4 without touching Diligent itself. The physics write-back
    // path (M2.1) is the one call site: Jolt hands back a dynamic body's world (position,
    // rotation) each tick, and the entity's EXISTING scale (physics doesn't simulate scale)
    // needs to be folded back in before handing the result to SetEntityWorldMatrix.
    Mat4 ComposeWorldMatrix(const Vec3 &position, const Quat &rotation, const Vec3 &scale);

    // Find the first entity carrying a LightComponent and report its world-space direction TO
    // the light (local +Z of its world matrix, see MakeLightTransform) plus color/intensity.
    // Leaves the out-params untouched and returns false if the scene has no light entity.
    // Uses cached world matrices (call after UpdateWorldTransforms).
    bool GetActiveLight(const Scene &scene, Vec3 &dirToLight, Vec3 &color, float &intensity);

    // --- Hierarchy mutations (editor operations; defined in scene.cpp) ----------
    // All keep the parents-before-children invariant (re-ordering as needed) and fix up
    // `selected`. They mutate the vector, so never call them mid-iteration over `entities`.

    // True if `idx` equals `maybeAncestor` or has it anywhere up its parent chain.
    bool IsAncestorOrSelf(const Scene &scene, int idx, int maybeAncestor);

    // Append a new child under `parent`, re-ordered to sit under its parent's subtree in the
    // outliner. Returns the new entity's index.
    int AddChildEntity(Scene &scene, int parent, const char *name);

    // Delete an entity and its whole subtree (no-op on the root).
    void DeleteEntity(Scene &scene, int idx);

    // Duplicate an entity + its subtree as a sibling (copies mesh/model handles + material;
    // models stay shared by handle). Returns the duplicate root's new index, or -1 on failure.
    int DuplicateEntity(Scene &scene, int idx);

    // Re-parent `idx` under `newParent`, preserving its WORLD transform — the local TRS is
    // rewritten (via the decompose) so the object doesn't jump. Refuses the root, cycles, and
    // no-ops. Returns true on success.
    bool ReparentEntity(Scene &scene, int idx, int newParent);

    // Move `idx` to be a sibling just before/after `target` (re-parenting first if needed, also
    // world-preserving).
    bool MoveEntityAsSibling(Scene &scene, int idx, int target, bool before);

    // Drop all entities. GPU meshes/models are owned by the Renderer and freed at its Shutdown,
    // so the scene only holds handles — nothing GPU-side to release here.
    inline void DestroyScene(Scene &scene) { scene.entities.clear(); }

} // namespace toon
