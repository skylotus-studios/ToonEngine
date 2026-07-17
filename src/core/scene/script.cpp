//============================================================================
//  core/scene/script.cpp — native script registry + per-tick dispatch.
//============================================================================
#include "core/scene/script.h"

#include "core/scene/scene.h" // Entity, Scene — the dispatch loops below walk their real fields

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace toon {

    namespace {

        // Function-local static: constructed on first use regardless of which translation
        // unit's static initializer (a script's self-registering static, e.g.
        // spin_script.cpp) runs first — sidesteps the static-initialization-order fiasco a
        // plain namespace-scope map would risk.
        std::unordered_map<std::string, ScriptFactory> &Registry() {
            static std::unordered_map<std::string, ScriptFactory> registry;
            return registry;
        }

    } // namespace

    // --- Registry ----------------------------------------------------------------

    void RegisterScript(const std::string &name, ScriptFactory factory) { Registry()[name] = std::move(factory); }

    std::unique_ptr<Script> CreateScript(const std::string &name) {
        const auto it = Registry().find(name);
        if (it == Registry().end()) {
            std::fprintf(stderr, "Unknown script type '%s'\n", name.c_str());
            return nullptr;
        }
        return it->second();
    }

    std::vector<std::string> GetRegisteredScriptNames() {
        std::vector<std::string> names;
        names.reserve(Registry().size());
        for (const auto &[name, factory] : Registry()) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end()); // stable, predictable order for the UI picker
        return names;
    }

    // --- Per-tick dispatch ---------------------------------------------------------

    void CreateScripts(Scene &scene) {
        for (Entity &e : scene.entities) {
            for (ScriptComponent &sc : e.scripts) {
                if (sc.instance) { sc.instance->OnCreate(e, scene); }
            }
        }
    }

    void UpdateScripts(Scene &scene, float dt) {
        for (Entity &e : scene.entities) {
            for (ScriptComponent &sc : e.scripts) {
                if (sc.instance) { sc.instance->OnUpdate(e, scene, dt); }
            }
        }
    }

} // namespace toon
