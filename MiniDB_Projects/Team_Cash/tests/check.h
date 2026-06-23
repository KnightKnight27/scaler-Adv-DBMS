// A tiny assert-based test harness. Each test file is its own program: it
// includes this header, runs CHECK(...) on its expectations, and ends with
// REPORT() which returns non-zero if anything failed.
#pragma once

#include <iostream>

namespace check {
inline int& fails() { static int f = 0; return f; }
}  // namespace check

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " << #cond << "\n"; \
            check::fails()++;                                                  \
        }                                                                      \
    } while (0)

#define REPORT()                                                               \
    do {                                                                       \
        if (check::fails()) {                                                  \
            std::cerr << check::fails() << " check(s) FAILED\n";               \
            return 1;                                                          \
        }                                                                      \
        std::cout << "OK: all checks passed (" << __FILE__ << ")\n";           \
        return 0;                                                              \
    } while (0)
