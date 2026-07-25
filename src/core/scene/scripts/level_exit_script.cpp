//============================================================================
//  core/scene/scripts/level_exit_script.cpp: LevelExitScript's hooks and self-registration.
//============================================================================
#include "core/scene/scripts/level_exit_script.h"

#include "core/platform/paths.h" // Assets::Scene (filename -> full path)
#include "core/scene/scene.h"    // RequestSceneChange

#include <memory>

namespace toon {

    namespace {
        // The .scene format is whitespace-delimited (see core/scene/serializer.cpp), so an empty
        // target would write nothing and leave Load's `in >> targetScene` reading the next line's
        // token. Round-trip it through a sentinel instead of silently corrupting the file.
        constexpr const char *kEmptyTarget = "-";
    } // namespace

    void LevelExitScript::OnCollisionEnter(Entity &, Scene &scene, int, const Vec3 &, const Vec3 &) {
        if (targetScene.empty()) { return; }
        // Only records the request. This runs inside the fixed sim tick's contact dispatch, with
        // Jolt still owning every body and this entity still being iterated -- swapping the scene
        // here is the exact use-after-free RequestSceneChange exists to prevent. Repeat calls
        // while the transition is already running are ignored by TickSceneTransition, so a player
        // standing in the trigger doesn't restart the fade every tick.
        RequestSceneChange(scene, Assets::Scene(targetScene).c_str());
    }

    void LevelExitScript::Save(std::ostream &out) const {
        out << ' ' << (targetScene.empty() ? kEmptyTarget : targetScene);
    }

    void LevelExitScript::Load(std::istream &in) {
        std::string token;
        in >> token;
        targetScene = (token == kEmptyTarget) ? std::string{} : token;
    }

    void RegisterLevelExitScript() {
        RegisterScript(kLevelExitScriptName, [] { return std::make_unique<LevelExitScript>(); });
    }

} // namespace toon
