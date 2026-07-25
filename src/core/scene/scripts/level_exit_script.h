#pragma once
//============================================================================
//  core/scene/scripts/level_exit_script.h: a door. Walking into this entity asks the engine to
//  swap to another level (roadmap #19).
//
//  The end-to-end proof that a level transition can be driven from gameplay code rather than the
//  editor's File menu: OnCollisionEnter fires from inside the physics tick, which is the single
//  most hostile place in the frame to destroy a scene, and RequestSceneChange makes that safe by
//  only writing a string -- the swap itself happens later, at a frame boundary, in
//  app/session.h's TickSceneTransition.
//
//  `targetScene` is a FILENAME under the scenes/ asset dir ("level_two.scene"), not a full path,
//  resolved through core/platform/paths.h's Assets::Scene -- the same split SpriteComponent::
//  texturePath already uses. An authored level therefore never bakes in a build-machine path and
//  still resolves from a packaged build running anywhere.
//============================================================================
#include "core/scene/script.h"

#include <string>

namespace toon {

    inline constexpr const char *kLevelExitScriptName = "LevelExit";

    class LevelExitScript : public Script {
    public:
        std::string targetScene;

        void OnCollisionEnter(Entity &self, Scene &scene, int other, const Vec3 &point, const Vec3 &normal) override;
        void Save(std::ostream &out) const override;
        void Load(std::istream &in) override;
    };

    // Add this type to the name registry. Called by RegisterBuiltinScripts (see
    // core/scene/scripts/builtin_scripts.h) rather than by a self-registering static.
    void RegisterLevelExitScript();

} // namespace toon
