#pragma once

// Minimal, dependency-free test helper. Each test is a plain executable that
// uses CHECK / CHECK_EQ and ends with `return minidb::test::summary("name");`.
// Keeps the build free of any external test framework.

#include <cstdio>
#include <string>

namespace minidb::test {
inline int g_failures = 0;

inline int summary(const char *name) {
  if (g_failures != 0) {
    std::printf("[%s] FAILED — %d check(s) failed\n", name, g_failures);
    return 1;
  }
  std::printf("[%s] OK\n", name);
  return 0;
}
}  // namespace minidb::test

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);   \
      ::minidb::test::g_failures++;                                            \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    auto _va = (a);                                                            \
    auto _vb = (b);                                                            \
    if (!(_va == _vb)) {                                                       \
      std::printf("  [FAIL] %s:%d  CHECK_EQ(%s, %s)\n", __FILE__, __LINE__,    \
                  #a, #b);                                                     \
      ::minidb::test::g_failures++;                                            \
    }                                                                          \
  } while (0)

#define CHECK_TRUE(cond) CHECK(cond)
#define CHECK_FALSE(cond) CHECK(!(cond))
