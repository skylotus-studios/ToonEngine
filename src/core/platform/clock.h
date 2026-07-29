#pragma once
//============================================================================
//  core/platform/clock.h: the engine's monotonic wall-clock source.
//
//  One function, for one reason: the fixed-timestep loop and every sim-clock reset around it
//  need "seconds since some fixed point," and the only source they had was glfwGetTime(). That
//  tied the simulation half of the engine -- app/app_state.cpp, app/session.cpp,
//  app/runtime_tick.cpp -- to a WINDOWING library for a timer, which is what blocked running
//  the sim with no window at all (app/sim_runtime.h). std::chrono::steady_clock is the same
//  monotonic timer with none of that baggage.
//
//  Diligent-free and GLFW-free by design, like its neighbour core/platform/paths.h: a standalone
//  platform utility, not part of any seam.
//============================================================================

namespace toon {
    namespace Clock {

        // Seconds since the first call, monotonic and never adjusted by the system clock (so a
        // daylight-saving jump or an NTP correction can't make a frame delta negative). The epoch
        // is arbitrary and deliberately unspecified: every caller either differences two readings
        // or resets a stored reading to "now", so only the DIFFERENCE is ever meaningful. That is
        // also why the swap away from glfwGetTime is invisible to the editor and player -- it
        // changes the epoch, and nothing reads the epoch.
        double Now();

    } // namespace Clock
} // namespace toon
