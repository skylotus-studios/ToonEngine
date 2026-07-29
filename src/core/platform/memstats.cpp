//============================================================================
//  core/platform/memstats.cpp: see memstats.h.
//============================================================================
#include "core/platform/memstats.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace toon {

    uint64_t PeakWorkingSetBytes() {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) { return 0; }
        return static_cast<uint64_t>(counters.PeakWorkingSetSize);
#else
        return 0; // Linux/macOS ports are planned but inactive (see CLAUDE.md)
#endif
    }

} // namespace toon
