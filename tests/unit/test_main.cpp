//============================================================================
//  tests/unit/test_main.cpp: ToonUnitTests entry point. See test_framework.h.
//
//  Runs every TOON_TEST-registered case, or only those whose name contains an optional
//  --filter=<substr> (CMakeLists.txt registers two CTest entries, "Math" and "SaveGame", that
//  use this to split the suite without needing two separate binaries).
//============================================================================
#include "test_framework.h"

#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    const char *filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--filter=", 9) == 0) { filter = argv[i] + 9; }
    }

    int ran = 0, failedTests = 0;
    for (const auto &c : toon::test::Registry()) {
        if (filter && c.name.find(filter) == std::string::npos) { continue; }

        ++ran;
        const int before = toon::test::FailureCount();
        std::printf("[ RUN      ] %s\n", c.name.c_str());
        c.fn();
        const bool passed = toon::test::FailureCount() == before;
        std::printf("[ %s ] %s\n", passed ? "PASS" : "FAIL", c.name.c_str());
        if (!passed) { ++failedTests; }
    }

    std::printf("---\n%d test(s) ran, %d failed%s%s\n", ran, failedTests, filter ? ", filter=" : "",
                filter ? filter : "");

    if (ran == 0) {
        std::fprintf(stderr, "ToonUnitTests: no tests matched%s%s\n", filter ? " filter " : " (empty registry)",
                     filter ? filter : "");
        return 1;
    }
    return failedTests == 0 ? 0 : 1;
}
