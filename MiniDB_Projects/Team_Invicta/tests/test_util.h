#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

// Minimal assertion harness for MiniDB tests. Each test file defines a series
// of CHECK(...) calls; on failure it prints the location and aborts with a
// non-zero exit code (so `make run-tests` reports the suite as failed).

static int g_checks = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "  CHECK FAILED: %s\n    at %s:%d\n", #cond,   \
                   __FILE__, __LINE__);                                   \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

#define CHECK_EQ(a, b)                                                    \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!((a) == (b))) {                                                  \
      std::fprintf(stderr, "  CHECK_EQ FAILED: %s == %s\n    at %s:%d\n", \
                   #a, #b, __FILE__, __LINE__);                           \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

#define TEST_PASS()                                                       \
  do {                                                                    \
    std::printf("  OK (%d checks)\n", g_checks);                          \
    return 0;                                                             \
  } while (0)

inline std::string TmpFile(const char *name) {
  return std::string("/tmp/minidb_") + name;
}
