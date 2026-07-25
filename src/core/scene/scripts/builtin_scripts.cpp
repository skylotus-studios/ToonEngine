//============================================================================
//  core/scene/scripts/builtin_scripts.cpp: see builtin_scripts.h.
//
//  One line per script type. These calls are what reference each script's TU, which is exactly
//  what makes the linker keep it (see the header for why that matters).
//============================================================================
#include "core/scene/scripts/builtin_scripts.h"

#include "core/scene/scripts/level_exit_script.h"
#include "core/scene/scripts/spin_script.h"

namespace toon {

    void RegisterBuiltinScripts() {
        RegisterSpinScript();
        RegisterLevelExitScript();
    }

} // namespace toon
