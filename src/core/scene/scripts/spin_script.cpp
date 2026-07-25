//============================================================================
//  core/scene/scripts/spin_script.cpp: SpinScript's OnUpdate/Save/Load and self-registration.
//============================================================================
#include "core/scene/scripts/spin_script.h"

#include "core/scene/scene.h" // Entity: this script's whole job is mutating its transform

#include <iomanip>
#include <memory>

namespace toon {

    void SpinScript::OnUpdate(Entity &self, Scene &, float dt) {
        if (!self.transform) {
            return; // anchor/grouping node, nothing to spin
        }
        // Pre-multiply a small delta rotation around the FIXED world-space `axis` onto the
        // entity's current orientation (core/math.h's hand-rolled Quat -- this file must stay
        // Diligent-free, see script.h's banner). Re-normalize every tick: repeated float
        // multiplication drifts a unit quaternion's length slightly, and an un-normalized
        // quaternion feeds a subtly wrong (non-rigid) matrix into LocalFromTransform.
        const Quat delta = QuatFromAxisAngle(axis, speed * dt);
        self.transform->rotation = Normalize(delta * self.transform->rotation);
    }

    // %.6f-equivalent precision (std::fixed + setprecision(6)) to match the rest of the
    // .scene format's WriteFloat/WriteVec3, not a bit-exact round-trip (that's a
    // rollback-netcode-grade concern, deliberately out of scope here), just enough to
    // avoid visible drift across repeated Play/Stop clones.
    void SpinScript::Save(std::ostream &out) const {
        out << std::fixed << std::setprecision(6) << ' ' << axis.x << ' ' << axis.y << ' ' << axis.z << ' ' << speed;
    }

    void SpinScript::Load(std::istream &in) { in >> axis.x >> axis.y >> axis.z >> speed; }

    void RegisterSpinScript() { RegisterScript(kSpinScriptName, [] { return std::make_unique<SpinScript>(); }); }

} // namespace toon
