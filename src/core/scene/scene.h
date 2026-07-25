#pragma once
//============================================================================
//  core/scene/scene.h: the scene graph, an entity tree with hierarchy-composed world
//  transforms.
//
//  Backend-agnostic like the rest of the seam vocabulary (toon::Transform + Mat4 +
//  opaque handles). No Diligent types leak here. The world-matrix composition (the
//  actual 4x4 math) lives in scene.cpp, which IS allowed to use Diligent (build-on-Diligent).
//  Adapted from ToonEngineOld/src/scene.
//============================================================================
#include "core/rendering/renderer.h"   // Transform, Material, Mesh/ModelHandle, Mat4 (via math.h)
#include "core/rendering/primitives.h" // PrimitiveDesc: mesh-regeneration params for serialization
#include "core/physics/physics.h"      // ColliderShape/BodyType/BodyHandle: Entity's physics components (M2.1)
#include "core/audio/audio.h"          // SoundHandle: Entity's AudioSource component (M2.2)
#include "core/scene/script.h"         // ScriptComponent: Entity's attached native scripts (M1.3)

#include <optional>
#include <string>
#include <vector>

namespace toon {

    // A directional light carried by an entity. Aimed by the entity's ROTATION (its local +Z
    // axis in world space is the direction light rays travel, Unity/Godot-style; rotate the
    // entity, e.g. with the gizmo, to re-aim it). See MakeLightTransform / GetActiveLight.
    struct LightComponent {
        Vec3 color = {1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
    };

    // A camera carried by an entity (roadmap #15): the viewpoint the RUNTIME renders from (the
    // editor uses its own orbit camera instead). Aimed by the entity's ROTATION exactly like
    // LightComponent -- its local +Z axis in world space is the look direction -- and POSITIONED
    // at the entity's world translation, so parenting a camera entity under a player entity is a
    // follow rig with no extra machinery. Holds only projection params; the pose comes from the
    // entity's worldMatrix (see GetActiveCamera), never duplicated here. The first entity whose
    // camera is `primary`, in scene order, is the one that renders.
    struct CameraComponent {
        float fovY = 1.0472f;      // vertical field of view, radians (~60 deg); ignored when orthographic
        float nearZ = 0.1f;
        float farZ = 100.0f;
        bool orthographic = false;
        float orthoHeight = 10.0f; // world-space vertical extent when orthographic (the fovY analog)
        bool primary = false;      // the view the runtime renders from; first primary in scene order wins
    };

    // A collision shape carried by an entity (M2.1): the physics seam's ColliderShape/Vec3
    // vocabulary (core/physics/physics.h). An entity with a collider but no RigidBodyComponent is an
    // implicit static collider (a wall/floor); paired with one, it becomes a dynamic/kinematic
    // mover; see RigidBodyComponent below.
    struct ColliderComponent {
        ColliderShape shape = ColliderShape::Box;
        Vec3 extents = {0.5f, 0.5f, 0.5f}; // meaning depends on shape; see BodyDesc
    };

    // Physics behavior for an entity that also carries a ColliderComponent (M2.1). `handle` is
    // runtime-only state (like Entity::worldMatrix): populated when the physics world is built
    // for a Play session (main.cpp) and never serialized; it copies as a plain id, which is
    // harmless since a copy is only ever a snapshot/backup (Play/Stop, DuplicateEntity) and the
    // real body is rebuilt the next time Play starts.
    struct RigidBodyComponent {
        BodyType type = BodyType::Dynamic;
        float mass = 1.0f; // ignored for Static/Kinematic; see BodyDesc
        float friction = 0.5f;
        float restitution = 0.2f;
        BodyHandle handle = BodyHandle::Invalid;
    };

    // A sound emitter carried by an entity (M2.2): the audio seam's SoundDesc vocabulary
    // (core/audio/audio.h). `clip` is a filename under the audio/ asset dir, same "asset path on the
    // component, GPU/audio resource behind a handle" split as Entity::modelPath/model. Autoplay
    // emitters start when a Play/Step session begins (app/audio_glue.cpp's BuildAudioWorld);
    // `handle` is runtime-only state (like RigidBodyComponent::handle above): populated then,
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
        float maxDistance = 25.0f; // spatial only; see SoundDesc
        SoundHandle handle = SoundHandle::Invalid;
    };

    // An animation clip playing on an entity's (skinned) model (roadmap #11: skeletal
    // animation). `clipIndex` indexes the model's own animation list (Renderer::
    // GetModelAnimationCount/Name); -1 = no clip selected, draws bind pose. Unlike
    // AudioSource/RigidBodyComponent above, this holds no runtime handle: the bone-matrix
    // palette is recomputed straight from the model + `time` at draw time (Renderer::DrawModel/
    // DrawModelShadow's AnimationState param), so there's nothing to build fresh when a Play
    // session starts and nothing to leave stale when one stops -- Stop's existing scene-backup
    // revert (see "Play/Pause/Step" in docs/architecture.md) already covers this component for
    // free, the same way it covers `transform`.
    struct AnimationComponent {
        int32_t clipIndex = -1;
        float time = 0.0f;
        // Previous fixed sim tick's `time`, for motion vectors (see Renderer::AnimationState);
        // snapshotted by SnapshotSimState, the same treatment prevSimTransform gets below.
        float prevTime = 0.0f;
        bool playing = true;
        bool looping = true;
    };

    // A flat, textured, alpha-blended quad carried by an entity (roadmap #13: 2D & sprites),
    // drawn transform-oriented (no billboarding) in a separate transparent pass after the
    // opaque toon pass; see Renderer::DrawSprite. Splits data the same way AudioSource above
    // does: `texturePath` is a FILENAME relative to the sprites/ asset dir (not a full path,
    // unlike modelPath/AudioSource::clip), serialized; `texture` is the runtime handle rebuilt
    // from it on load via Assets::Sprite (core/platform/paths.h), never serialized, harmless to copy as a plain
    // id -- same reasoning as AudioSource::handle). `uvRect` is an atlas sub-rect (xy =
    // offset, zw = scale, default the full [0,1] texture); `flipX`/`flipY` mirror it by
    // negating the relevant axis's offset/scale (applied by the app layer before DrawSprite,
    // ToonEngineOld's convention, not branched in the shader).
    struct SpriteComponent {
        std::string texturePath;
        TextureHandle texture = TextureHandle::Invalid;
        Vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
        Vec4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
        bool flipX = false;
        bool flipY = false;
    };

    // (A sprite's user-facing texture filename -- SpriteComponent::texturePath, e.g. "icon.png"
    // -- is resolved against the one sprites/ asset directory by Assets::Sprite in
    // core/platform/paths.h, so authoring a sprite never needs a full path typed in by hand.)

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

        // Camera (roadmap #15): set -> this entity is a viewpoint the runtime can render from.
        // Positioned/aimed by the entity's transform (see CameraComponent / GetActiveCamera).
        std::optional<CameraComponent> camera;

        // Physics (M2.1): a collider alone is a static collider (see ColliderComponent above);
        // paired with a RigidBodyComponent, the entity becomes a dynamic/kinematic mover.
        std::optional<ColliderComponent> collider;
        std::optional<RigidBodyComponent> body;

        // Audio (M2.2): a positional or non-positional sound emitter; see AudioSource above.
        std::optional<AudioSource> audioSource;

        // Skeletal animation (roadmap #11): which clip of `model`'s own animation list is
        // playing, and when; see AnimationComponent above. Only meaningful when `model` is
        // set and Renderer::ModelHasSkin(model) is true.
        std::optional<AnimationComponent> animation;

        // 2D sprite (roadmap #13): a flat textured quad at this entity's transform,
        // independent of mesh/model (nothing stops an entity from carrying both, though the
        // demo scene never does). See SpriteComponent above.
        std::optional<SpriteComponent> sprite;

        // Attached native scripts (core/scene/script.h): per-tick gameplay hooks (M1.3). A
        // std::unique_ptr member makes ScriptComponent, and therefore Entity, NOT implicitly
        // copyable: the copy constructor/assignment below deep-clone each script via the
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

        // A gameplay script's request to replace this whole scene with another (roadmap #19).
        // Runtime-only, never serialized -- the same treatment `selected` above already gets.
        // Written by RequestSceneChange below and drained at a frame boundary by app/session.h's
        // TickSceneTransition; NEVER acted on where it's set. Empty = nothing pending.
        std::string requestedScenePath;
    };

    // --- Scene change requests (roadmap #19) --------------------------------------
    // The one route gameplay code has to ask for a level change. A Script only ever receives
    // `Entity &self` and `Scene &scene` (core/scene/script.h) -- there is no context pointer or
    // service locator, deliberately -- so the request rides on the Scene a script already holds.
    //
    // Requesting and performing are split because a script runs INSIDE UpdateScripts' walk of
    // `entities`: swapping the scene there would free the very entity whose OnUpdate is still on
    // the stack, leaving `self` dangling as execution returns into engine code. Godot defers
    // change_scene_to_file for exactly this reason ("it may still be executing code"); Unreal's
    // OpenLevel likewise only sets a travel URL that the next tick acts on. The same split also
    // makes the request safe from OnCollision*, which fires while Jolt still owns every body.

    // Ask for `path` to replace this scene. Last writer wins if called twice in one tick; a null
    // or empty path clears any pending request.
    void RequestSceneChange(Scene &scene, const char *path);

    // True if a scene change is pending. Read by the fixed-step loop (to stop simulating a scene
    // that's about to be destroyed) and by TickSceneTransition (to start the swap).
    bool HasPendingSceneChange(const Scene &scene);

    // Ensure a root exists at index 0 (parent = -1, no transform). Call once, before adding
    // entities; existing entities are shifted down and re-parented to the new root.
    void EnsureSceneRoot(Scene &scene);

    // Append a new entity under `parent` (defaults to the root on a bad index) and return its
    // index. Build the scene parent-first so the parents-before-children invariant holds.
    int AddEntity(Scene &scene, int parent, const char *name);

    // Recompute every entity's cached worldMatrix from the hierarchy (one forward pass, since
    // parents precede children), snapshotting the previous frame's matrices first. Each entity's
    // local pose is interpolated between prevSimTransform and transform by `alpha` (the fixed-
    // timestep accumulator's fraction into the next sim tick; see main.cpp's frame loop), the
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

    // Compose a world Mat4 from a position + quaternion rotation + scale: lets a Diligent-free
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

    // Find the first entity carrying a `primary` CameraComponent and fill `out` with the view it
    // defines: eye at the entity's world position, look direction its local +Z (same convention
    // as GetActiveLight), plus the component's projection params. Leaves `out` untouched and
    // returns false if the scene has no primary camera (the runtime then keeps its fallback
    // view). Uses cached world matrices (call after UpdateWorldTransforms).
    bool GetActiveCamera(const Scene &scene, Camera &out);

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

    // Re-parent `idx` under `newParent`, preserving its WORLD transform: the local TRS is
    // rewritten (via the decompose) so the object doesn't jump. Refuses the root, cycles, and
    // no-ops. Returns true on success.
    bool ReparentEntity(Scene &scene, int idx, int newParent);

    // Move `idx` to be a sibling just before/after `target` (re-parenting first if needed, also
    // world-preserving).
    bool MoveEntityAsSibling(Scene &scene, int idx, int target, bool before);

    // Drop all entities. GPU meshes/models are owned by the Renderer and freed at its Shutdown,
    // so the scene only holds handles; nothing GPU-side to release here.
    inline void DestroyScene(Scene &scene) { scene.entities.clear(); }

} // namespace toon
