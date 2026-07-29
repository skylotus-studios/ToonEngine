#pragma once
//============================================================================
//  core/platform/memstats.h: process memory stats, for app/metrics.h's alloc.peak_bytes.
//
//  A different concern from core/platform/paths.h (asset location) and core/platform/clock.h
//  (timing), so it gets its own file rather than joining either. Windows-only today, same
//  platform-support boundary paths.cpp's ExecutableDir already draws (Linux/macOS ports are
//  planned but inactive, see CLAUDE.md's Platform Support table): 0 on any other platform.
//============================================================================
#include <cstdint>

namespace toon {

    // Peak working-set size (bytes) of the CURRENT PROCESS since it started, via Windows'
    // GetProcessMemoryInfo. This is whole-process memory -- Diligent, Jolt, the CRT, every DLL,
    // not an engine-specific allocator figure -- so it's a real, useful upper bound, not a
    // precise "the engine allocated N bytes" count. That finer number would need a global
    // operator new/delete override (app/metrics.h's alloc.count_after_init, left null: a
    // separate, more invasive follow-up). Returns 0 on failure or on a non-Windows build.
    uint64_t PeakWorkingSetBytes();

} // namespace toon
