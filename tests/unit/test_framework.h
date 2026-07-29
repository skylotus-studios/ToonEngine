#pragma once
//============================================================================
//  tests/unit/test_framework.h: a hand-rolled ~50-line test registry.
//
//  No new dependency (no gtest/Catch2) -- matches scripts/metrics_diff.py's own "pure stdlib"
//  posture, just in C++, and this repo's "no vcpkg" rule 5. TOON_TEST(name) self-registers via
//  a static initializer (the same pattern DiligentFX-adjacent registries use); CHECK/CHECK_NEAR
//  record a failure and keep running the rest of the test body, the same "collect everything,
//  report at the end" shape scripts/metrics_diff.py's `breaches` list already uses, so one test
//  reports every failing assertion in it, not just the first.
//============================================================================
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace toon::test {

    struct Case {
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<Case> &Registry() {
        static std::vector<Case> registry;
        return registry;
    }

    inline int &FailureCount() {
        static int count = 0;
        return count;
    }

    struct Registrar {
        Registrar(const char *name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
    };

} // namespace toon::test

// One translation unit may register multiple tests; TOON_CONCAT gives each static Registrar a
// unique name keyed off __LINE__ (the same trick BOOST_PP/gtest macros use).
#define TOON_CONCAT_INNER(a, b) a##b
#define TOON_CONCAT(a, b) TOON_CONCAT_INNER(a, b)

#define TOON_TEST(name)                                                                                              \
    static void TOON_CONCAT(ToonTestFn_, __LINE__)();                                                                \
    static ::toon::test::Registrar TOON_CONCAT(ToonTestReg_, __LINE__)(name, TOON_CONCAT(ToonTestFn_, __LINE__));    \
    static void TOON_CONCAT(ToonTestFn_, __LINE__)()

#define CHECK(cond)                                                                                                  \
    do {                                                                                                             \
        if (!(cond)) {                                                                                               \
            std::fprintf(stderr, "    CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
            ++::toon::test::FailureCount();                                                                          \
        }                                                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                        \
    do {                                                                                                             \
        const double _a = (a), _b = (b), _eps = (eps);                                                              \
        if (std::fabs(_a - _b) > _eps) {                                                                             \
            std::fprintf(stderr, "    CHECK_NEAR failed at %s:%d: %s (%.6f) vs %s (%.6f), eps=%.6f\n", __FILE__,    \
                        __LINE__, #a, _a, #b, _b, _eps);                                                             \
            ++::toon::test::FailureCount();                                                                          \
        }                                                                                                            \
    } while (0)
