#pragma once
//============================================================================
//  core/scene/scripts/spin_script.h: the first concrete Script, spins an entity's local
//  rotation around a fixed axis at a constant rate.
//
//  Ports main.cpp's old hardcoded spin block (the "Spinner" side-list) onto the
//  general script system as its end-to-end proof: the same incremental spin, now living
//  on the entity instead of an external index list, so it survives reparent/reload/Stop
//  with no side-list bookkeeping.
//============================================================================
#include "core/math.h"
#include "core/scene/script.h"

namespace toon {

    inline constexpr const char *kSpinScriptName = "Spin";

    class SpinScript : public Script {
    public:
        Vec3 axis = {0.0f, 1.0f, 0.0f};
        float speed = 0.6f; // radians/sec

        void OnUpdate(Entity &self, Scene &scene, float dt) override;
        void Save(std::ostream &out) const override;
        void Load(std::istream &in) override;
    };

    // Add this type to the name registry. Called by RegisterBuiltinScripts (see
    // core/scene/scripts/builtin_scripts.h) rather than by a self-registering static.
    void RegisterSpinScript();

} // namespace toon
