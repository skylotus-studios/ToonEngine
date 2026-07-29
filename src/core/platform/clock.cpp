//============================================================================
//  core/platform/clock.cpp: see clock.h.
//============================================================================
#include "core/platform/clock.h"

#include <chrono>

namespace toon {
    namespace Clock {

        double Now() {
            using Steady = std::chrono::steady_clock;
            // Function-local static: the epoch is fixed at the FIRST call rather than at static-
            // init time, so the returned values stay small regardless of how long the process has
            // been up before the first tick. Initialization is thread-safe (C++11 magic statics),
            // though nothing calls this off the main thread today.
            static const Steady::time_point start = Steady::now();
            return std::chrono::duration<double>(Steady::now() - start).count();
        }

    } // namespace Clock
} // namespace toon
