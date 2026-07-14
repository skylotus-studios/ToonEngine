//============================================================================
//  core/scripts/spin_script.cpp
//============================================================================
#include "core/scripts/spin_script.h"

#include "core/scene.h" // Entity — this script's whole job is mutating its transform

#include <iomanip>
#include <memory>

namespace toon {

void SpinScript::OnUpdate(Entity& self, Scene&, float dt) {
    if (!self.transform) return; // anchor/grouping node — nothing to spin
    self.transform->rotationEuler = self.transform->rotationEuler + axis * (speed * dt);
}

// %.6f-equivalent precision (std::fixed + setprecision(6)) to match the rest of the
// .scene format's WriteFloat/WriteVec3 — not a bit-exact round-trip (that's a
// rollback-netcode-grade concern, deliberately out of scope here), just enough to
// avoid visible drift across repeated Play/Stop clones.
void SpinScript::Save(std::ostream& out) const {
    out << std::fixed << std::setprecision(6) << ' ' << axis.x << ' ' << axis.y << ' ' << axis.z << ' ' << speed;
}

void SpinScript::Load(std::istream& in) { in >> axis.x >> axis.y >> axis.z >> speed; }

namespace {
// Self-registers on program start (before main()) so the name -> factory registry
// can reconstruct a SpinScript from a saved scene or an in-memory Entity clone
// without either of those call sites knowing this type exists.
const bool kSpinScriptRegistered = [] {
    RegisterScript(kSpinScriptName, [] { return std::make_unique<SpinScript>(); });
    return true;
}();
} // namespace

} // namespace toon
