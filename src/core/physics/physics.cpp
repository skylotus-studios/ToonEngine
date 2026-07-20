//============================================================================
//  core/physics.cpp — Jolt Physics backend behind the physics seam.
//
//  The one translation unit allowed to include a Jolt header or name a JPH:: type — twin
//  to core/renderer.cpp's relationship with Diligent (see core/physics.h's banner). The
//  boilerplate below (layers, filters, the Init/Shutdown sequence) mirrors Jolt's own
//  HelloWorld sample (external/JoltPhysics/HelloWorld/HelloWorld.cpp) — read directly from
//  the vendored 5.6 headers/sample rather than assumed, since this Jolt version is newer
//  than most published tutorials (it ships hair/vehicle/soft-body modules ToonEngine
//  doesn't use — see CMakeLists.txt's JPH_USE_* trimming).
//============================================================================
#include "core/physics/physics.h"

// Jolt headers must not be included before Jolt.h (its own house rule).
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h> // OnContactAdded/Persisted/Removed -- see PhysicsWorld::Impl
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

// Disables the common clang/MSVC warnings Jolt's own headers are known to trigger (see
// Jolt's Core.h) — the same role the DILIGENT_NO_* trimming plays for Diligent.
JPH_SUPPRESS_WARNINGS

#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace toon {

namespace {

// --- Object / broadphase layers -----------------------------------------------
// Two layers, exactly Jolt's own HelloWorld sample: non-moving bodies never test against
// each other (skip that broadphase work entirely), everything else collides with
// everything. A richer per-body-type scheme is future work, only if profiling ever shows
// this one costing something with a large scene.
namespace Layers {
    constexpr JPH::ObjectLayer NonMoving = 0;
    constexpr JPH::ObjectLayer Moving = 1;
    constexpr JPH::uint NumLayers = 2;
} // namespace Layers

namespace BroadPhaseLayers {
    constexpr JPH::BroadPhaseLayer NonMoving(0);
    constexpr JPH::BroadPhaseLayer Moving(1);
    constexpr JPH::uint NumLayers = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterfaceImpl() {
        m_objectToBroadPhase[Layers::NonMoving] = BroadPhaseLayers::NonMoving;
        m_objectToBroadPhase[Layers::Moving] = BroadPhaseLayers::Moving;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NumLayers; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return m_objectToBroadPhase[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    // Jolt's own PROFILER_IN_DEBUG_AND_RELEASE (CMakeLists.txt, default ON) defines
    // JPH_PROFILE_ENABLED for this Debug build, which makes this pure virtual too —
    // omitting it (as an earlier draft of this file did) makes the class abstract and
    // fails to compile with "field type ... is an abstract class".
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(layer)) {
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NonMoving):
                return "NonMoving";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Moving):
                return "Moving";
            default:
                JPH_ASSERT(false);
                return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_objectToBroadPhase[Layers::NumLayers];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override {
        if (layer1 == Layers::NonMoving) { return layer2 == BroadPhaseLayers::Moving; }
        return true;
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object1, JPH::ObjectLayer object2) const override {
        if (object1 == Layers::NonMoving) { return object2 == Layers::Moving; }
        return true;
    }
};

// toon:: <-> Jolt conversions — trivial field copies, the same style as scene.cpp's
// ToMat4/ToFloat4x4 (toon <-> Diligent). One ToVec3 overload covers both JPH::Vec3Arg and
// JPH::RVec3Arg: ToonEngine doesn't build Jolt with JPH_DOUBLE_PRECISION, so RVec3Arg is
// literally Vec3Arg (see Jolt/Math/Real.h) — a second overload for it is a redefinition,
// not a distinct type, in this configuration.
JPH::Vec3 ToJoltVec3(const Vec3 &v) { return JPH::Vec3(v.x, v.y, v.z); }
Vec3      ToVec3(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat ToJoltQuat(const Quat &q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
Quat      ToQuat(JPH::QuatArg q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

JPH::EMotionType ToJoltMotionType(BodyType type) {
    switch (type) {
        case BodyType::Static:
            return JPH::EMotionType::Static;
        case BodyType::Kinematic:
            return JPH::EMotionType::Kinematic;
        case BodyType::Dynamic:
        default:
            return JPH::EMotionType::Dynamic;
    }
}

} // namespace

// --- PhysicsWorld::Impl --------------------------------------------------------
// The filters/layer-interface are held BY VALUE here (not stack locals in Init, unlike
// HelloWorld's main()) because PhysicsSystem::Init only takes a reference to them and
// requires them to outlive the PhysicsSystem (see its own doc comment) — Impl's lifetime
// already matches that requirement.
//
// Also IS-A JPH::ContactListener (registered via SetContactListener in Init), rather than a
// separate class, since it already owns everything a listener needs to resolve/queue events
// (`bodies`) and there is exactly one listener per world -- a second class here would only add
// an indirection.
struct PhysicsWorld::Impl : public JPH::ContactListener {
    JPH::TempAllocatorImpl *tempAllocator = nullptr;
    JPH::JobSystemThreadPool *jobSystem = nullptr;
    BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;
    JPH::PhysicsSystem physicsSystem;

    // BodyHandle (this seam's opaque id) -> the real JPH::BodyID it names.
    std::unordered_map<uint32_t, JPH::BodyID> bodies;
    uint32_t nextHandle = 1; // 0 is BodyHandle::Invalid

    // --- Contact events (ContactEvent, physics.h) -----------------------------------------
    // The JPH::ContactListener overrides below are called by Jolt from its own job-system
    // worker threads, DURING physicsSystem.Update (see ContactListener.h's class comment) --
    // never from this engine's usual single (main) thread. contactMutex guards pendingEvents
    // and lastContactGeometry from that concurrent access; ConsumeContactEvents (called on the
    // main thread, only after Update has returned and every worker is done) is the only
    // reader, so nothing here needs to be thread-safe beyond the mutex itself.
    std::mutex contactMutex;
    std::vector<ContactEvent> pendingEvents;

    struct ContactGeom {
        Vec3 point;
        Vec3 normal;
    };
    // Last point/normal seen for a still-touching sub-shape pair, keyed the same way Jolt's own
    // ContactListenerImpl sample does (Samples/Utils/ContactListenerImpl.h). OnContactRemoved's
    // own doc comment says the bodies may already be destroyed by the time it fires, so an Exit
    // event reports whatever Enter/Stay last recorded for that pair instead of live geometry.
    std::map<JPH::SubShapeIDPair, ContactGeom> lastContactGeometry;

    // Resolve a JPH::BodyID back to this seam's opaque handle -- same linear-scan-over-`bodies`
    // approach Raycast already uses below, fine at today's body counts. Safe to call from a
    // worker thread with no extra locking: the only writers of `bodies` (CreateBody/DestroyBody)
    // run on the main thread, which is blocked inside physicsSystem.Update() for this callback's
    // entire lifetime, so there is no concurrent writer to race against.
    BodyHandle ResolveHandle(JPH::BodyID id) const {
        for (const auto &[h, bodyId] : bodies) {
            if (bodyId == id) { return static_cast<BodyHandle>(h); }
        }
        return BodyHandle::Invalid;
    }

    // Shared body of OnContactAdded/OnContactPersisted: both report the same manifold shape,
    // differing only in the ContactPhase they queue.
    void RecordContact(const JPH::Body &body1, const JPH::Body &body2, const JPH::ContactManifold &manifold,
                       ContactPhase phase) {
        const JPH::SubShapeIDPair key(body1.GetID(), manifold.mSubShapeID1, body2.GetID(), manifold.mSubShapeID2);
        // Index 0: a manifold always has at least one contact point (that's what makes it a
        // manifold); this seam reports one representative point/normal per pair, not the full set.
        const Vec3 point = ToVec3(manifold.GetWorldSpaceContactPointOn1(0));
        // "direction along which to move body 2 out of collision" (ContactManifold's own
        // comment) -- i.e. away from body1, which is exactly physics.h's documented `a`->`b`
        // convention once body1/body2 below are assigned to a/b.
        const Vec3 normal = ToVec3(manifold.mWorldSpaceNormal);

        const std::lock_guard<std::mutex> lock(contactMutex);
        lastContactGeometry[key] = {point, normal};
        ContactEvent ev;
        ev.a = ResolveHandle(body1.GetID());
        ev.b = ResolveHandle(body2.GetID());
        ev.phase = phase;
        ev.point = point;
        ev.normal = normal;
        pendingEvents.push_back(ev);
    }

    void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold,
                        JPH::ContactSettings &ioSettings) override {
        RecordContact(inBody1, inBody2, inManifold, ContactPhase::Enter);
    }

    void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold,
                            JPH::ContactSettings &ioSettings) override {
        RecordContact(inBody1, inBody2, inManifold, ContactPhase::Stay);
    }

    // No Body available here at all (see this override's declaration in ContactListener.h) --
    // only a SubShapeIDPair naming the two BodyIDs, so Exit falls back to the last point/normal
    // RecordContact cached for this pair above.
    void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override {
        const std::lock_guard<std::mutex> lock(contactMutex);
        ContactGeom geom{}; // zeroed if this pair was never actually recorded (shouldn't happen)
        const auto it = lastContactGeometry.find(inSubShapePair);
        if (it != lastContactGeometry.end()) {
            geom = it->second;
            lastContactGeometry.erase(it);
        }
        ContactEvent ev;
        ev.a = ResolveHandle(inSubShapePair.GetBody1ID());
        ev.b = ResolveHandle(inSubShapePair.GetBody2ID());
        ev.phase = ContactPhase::Exit;
        ev.point = geom.point;
        ev.normal = geom.normal;
        pendingEvents.push_back(ev);
    }
};

PhysicsWorld::PhysicsWorld() : m_impl(new Impl()) {}

PhysicsWorld::~PhysicsWorld() { delete m_impl; }

bool PhysicsWorld::Init() {
    // Process-global Jolt setup: allocator hook, the factory (deserialization support —
    // unused today, but RegisterTypes needs it to exist), then the type registry itself.
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    // 10 MB is Jolt's own HelloWorld starting point ("way too much for this example but a
    // typical value") — revisit only if a profiled Step ever reports running out.
    m_impl->tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    m_impl->jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                      static_cast<int>(std::thread::hardware_concurrency()) - 1);

    // Small fixed budgets — this is a toon-styled editor demo scene, not an open world;
    // raise these if a scene ever actually needs more bodies/contacts than this.
    constexpr JPH::uint kMaxBodies = 1024;
    constexpr JPH::uint kNumBodyMutexes = 0; // 0 = Jolt picks a default
    constexpr JPH::uint kMaxBodyPairs = 1024;
    constexpr JPH::uint kMaxContactConstraints = 1024;
    m_impl->physicsSystem.Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
                               m_impl->broadPhaseLayerInterface, m_impl->objectVsBroadPhaseLayerFilter,
                               m_impl->objectLayerPairFilter);
    // Impl IS-A JPH::ContactListener (see its own comment) -- Jolt only supports one listener
    // per PhysicsSystem, which matches this seam's one-PhysicsWorld-per-Impl shape exactly.
    m_impl->physicsSystem.SetContactListener(m_impl);
    return true;
}

void PhysicsWorld::Shutdown() {
    Clear();
    delete m_impl->jobSystem;
    m_impl->jobSystem = nullptr;
    delete m_impl->tempAllocator;
    m_impl->tempAllocator = nullptr;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void PhysicsWorld::Clear() {
    JPH::BodyInterface &bodyInterface = m_impl->physicsSystem.GetBodyInterface();
    for (const auto &[handle, id] : m_impl->bodies) {
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }
    m_impl->bodies.clear();
}

BodyHandle PhysicsWorld::CreateBody(const BodyDesc &desc) {
    // BodyCreationSettings takes ownership via Jolt's intrusive refcounting (RefConst) the
    // moment it's constructed below, so a freshly-`new`'d shape needs no manual delete —
    // the same pattern Jolt's own HelloWorld uses for its sphere (`new SphereShape(0.5f)`).
    JPH::Shape *shape = nullptr;
    switch (desc.shape) {
        case ColliderShape::Box:
            shape = new JPH::BoxShape(ToJoltVec3(desc.extents));
            break;
        case ColliderShape::Sphere:
            shape = new JPH::SphereShape(desc.extents.x);
            break;
        case ColliderShape::Capsule:
            shape = new JPH::CapsuleShape(desc.extents.x, desc.extents.y);
            break;
    }

    JPH::BodyCreationSettings settings(shape, ToJoltVec3(desc.position), ToJoltQuat(desc.rotation),
                                       ToJoltMotionType(desc.type),
                                       desc.type == BodyType::Static ? Layers::NonMoving : Layers::Moving);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    if (desc.type != BodyType::Static && desc.mass > 0.0f) {
        // Keep the shape's own inertia distribution, just scale it to the requested mass —
        // simpler and more robust than hand-supplying an inertia tensor (MassAndInertiaProvided).
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    JPH::BodyInterface &bodyInterface = m_impl->physicsSystem.GetBodyInterface();
    const JPH::BodyID id = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        std::fprintf(stderr, "PhysicsWorld::CreateBody: out of bodies (raise kMaxBodies)\n");
        return BodyHandle::Invalid;
    }

    const uint32_t handle = m_impl->nextHandle++;
    m_impl->bodies[handle] = id;
    return static_cast<BodyHandle>(handle);
}

void PhysicsWorld::DestroyBody(BodyHandle body) {
    const auto it = m_impl->bodies.find(static_cast<uint32_t>(body));
    if (it == m_impl->bodies.end()) { return; }

    JPH::BodyInterface &bodyInterface = m_impl->physicsSystem.GetBodyInterface();
    bodyInterface.RemoveBody(it->second);
    bodyInterface.DestroyBody(it->second);
    m_impl->bodies.erase(it);
}

void PhysicsWorld::SetBodyTransform(BodyHandle body, const Vec3 &position, const Quat &rotation) {
    const auto it = m_impl->bodies.find(static_cast<uint32_t>(body));
    if (it == m_impl->bodies.end()) { return; }

    m_impl->physicsSystem.GetBodyInterface().SetPositionAndRotation(
        it->second, ToJoltVec3(position), ToJoltQuat(rotation), JPH::EActivation::Activate);
}

bool PhysicsWorld::GetBodyTransform(BodyHandle body, Vec3 &outPosition, Quat &outRotation) const {
    const auto it = m_impl->bodies.find(static_cast<uint32_t>(body));
    if (it == m_impl->bodies.end()) { return false; }

    // GetPositionAndRotation, NOT GetCenterOfMassPosition: the latter returns the shape's
    // center of mass, which only coincides with the body origin for a symmetric shape with
    // no manual mass override — GetPositionAndRotation is always the body's actual placement,
    // matching what an entity's Transform.position/rotation conceptually mean.
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_impl->physicsSystem.GetBodyInterface().GetPositionAndRotation(it->second, position, rotation);
    outPosition = ToVec3(position);
    outRotation = ToQuat(rotation);
    return true;
}

void PhysicsWorld::Step(float dt) {
    // 1 collision step per call: correct as long as the caller passes Jolt's own
    // recommended fixed 1/60 s tick (dt larger than that needs more steps to stay stable —
    // see PhysicsSystem::Update's own doc comment).
    m_impl->physicsSystem.Update(dt, 1, m_impl->tempAllocator, m_impl->jobSystem);
}

void PhysicsWorld::SetGravity(const Vec3 &gravity) { m_impl->physicsSystem.SetGravity(ToJoltVec3(gravity)); }

bool PhysicsWorld::Raycast(const Vec3 &origin, const Vec3 &direction, RaycastHit &outHit) const {
    const JPH::RRayCast ray(ToJoltVec3(origin), ToJoltVec3(direction));
    JPH::RayCastResult hit;
    if (!m_impl->physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit)) { return false; }

    // Map the hit JPH::BodyID back to our opaque handle. A linear scan over `bodies` is
    // fine at today's body counts (this seam ships Raycast now; nothing calls it yet — see
    // physics.h) — revisit with a reverse map only if that stops being true.
    BodyHandle handle = BodyHandle::Invalid;
    for (const auto &[h, id] : m_impl->bodies) {
        if (id == hit.mBodyID) {
            handle = static_cast<BodyHandle>(h);
            break;
        }
    }

    const JPH::RVec3 hitPoint = ray.GetPointOnRay(hit.mFraction);
    JPH::Vec3 normal = JPH::Vec3::sZero();
    {
        // Body::GetWorldSpaceSurfaceNormal needs a locked body, per NarrowPhaseQuery::CastRay's
        // own doc comment.
        const JPH::BodyLockRead lock(m_impl->physicsSystem.GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) { normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPoint); }
    }

    outHit.body = handle;
    outHit.point = ToVec3(hitPoint);
    outHit.normal = ToVec3(normal);
    outHit.distance = hit.mFraction * Length(direction);
    return true;
}

std::vector<ContactEvent> PhysicsWorld::ConsumeContactEvents() {
    const std::lock_guard<std::mutex> lock(m_impl->contactMutex);
    std::vector<ContactEvent> events;
    events.swap(m_impl->pendingEvents); // pendingEvents left empty, ready for the next tick
    return events;
}

// Pure geometry -- no Jolt dependency, despite living in this Jolt-only file (see the
// declaration's own comment in physics.h for why it's still declared there).
std::vector<Vec3> ColliderWireframe(ColliderShape shape, const Vec3 &extents) {
    std::vector<Vec3> lines; // consecutive pairs: [0]-[1], [2]-[3], ...

    const auto addSegment = [&lines](const Vec3 &a, const Vec3 &b) {
        lines.push_back(a);
        lines.push_back(b);
    };

    constexpr float kPi = 3.14159265f;

    switch (shape) {
        case ColliderShape::Box: {
            // 8 corners, named by sign: c<x><y><z>, 0 = negative, 1 = positive.
            const float x = extents.x, y = extents.y, z = extents.z;
            const Vec3 c000{-x, -y, -z}, c001{-x, -y, z}, c010{-x, y, -z}, c011{-x, y, z};
            const Vec3 c100{x, -y, -z}, c101{x, -y, z}, c110{x, y, -z}, c111{x, y, z};
            addSegment(c000, c100);
            addSegment(c100, c101);
            addSegment(c101, c001);
            addSegment(c001, c000); // bottom face (-y)
            addSegment(c010, c110);
            addSegment(c110, c111);
            addSegment(c111, c011);
            addSegment(c011, c010); // top face (+y)
            addSegment(c000, c010);
            addSegment(c100, c110);
            addSegment(c101, c111);
            addSegment(c001, c011); // verticals
            break;
        }
        case ColliderShape::Sphere: {
            // 3 orthogonal great circles (XY, XZ, YZ planes) -- reads as a sphere from any
            // angle without needing a full latitude/longitude mesh.
            const float r = extents.x;
            constexpr int kSegments = 24;
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = (2.0f * kPi * static_cast<float>(i)) / kSegments;
                const float a1 = (2.0f * kPi * static_cast<float>(i + 1)) / kSegments;
                const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                addSegment({r * c0, r * s0, 0.0f}, {r * c1, r * s1, 0.0f});     // XY
                addSegment({r * c0, 0.0f, r * s0}, {r * c1, 0.0f, r * s1});     // XZ
                addSegment({0.0f, r * c0, r * s0}, {0.0f, r * c1, r * s1});     // YZ
            }
            break;
        }
        case ColliderShape::Capsule: {
            // extents.x = half-height (along local Y, matching Jolt's CapsuleShape), extents.y
            // = radius. Two rings at +-halfHeight, 4 vertical struts, and a top/bottom
            // hemisphere each suggested by 2 perpendicular half-circle arcs (XY and YZ planes).
            const float halfHeight = extents.x;
            const float r = extents.y;
            constexpr int kSegments = 24;
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = (2.0f * kPi * static_cast<float>(i)) / kSegments;
                const float a1 = (2.0f * kPi * static_cast<float>(i + 1)) / kSegments;
                const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                addSegment({r * c0, halfHeight, r * s0}, {r * c1, halfHeight, r * s1});
                addSegment({r * c0, -halfHeight, r * s0}, {r * c1, -halfHeight, r * s1});
            }
            for (int k = 0; k < 4; ++k) {
                const float a = (kPi * 0.5f) * static_cast<float>(k);
                const Vec3 top{r * std::cos(a), halfHeight, r * std::sin(a)};
                const Vec3 bottom{r * std::cos(a), -halfHeight, r * std::sin(a)};
                addSegment(top, bottom);
            }
            constexpr int kHalfSegments = kSegments / 2;
            for (int i = 0; i < kHalfSegments; ++i) {
                const float a0 = (kPi * static_cast<float>(i)) / kHalfSegments;
                const float a1 = (kPi * static_cast<float>(i + 1)) / kHalfSegments;
                const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                // Top cap: arcs from the top ring's angle-0 point, over the pole, to angle-pi.
                addSegment({r * c0, halfHeight + r * s0, 0.0f}, {r * c1, halfHeight + r * s1, 0.0f});     // XY
                addSegment({0.0f, halfHeight + r * s0, r * c0}, {0.0f, halfHeight + r * s1, r * c1});     // YZ
                // Bottom cap: mirrored, arcing downward.
                addSegment({r * c0, -halfHeight - r * s0, 0.0f}, {r * c1, -halfHeight - r * s1, 0.0f});   // XY
                addSegment({0.0f, -halfHeight - r * s0, r * c0}, {0.0f, -halfHeight - r * s1, r * c1});   // YZ
            }
            break;
        }
    }

    return lines;
}

} // namespace toon
