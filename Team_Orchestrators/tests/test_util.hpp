#pragma once
// Minimal test harness: CHECK macros + a summary that sets the process exit
// code (ctest reads the exit code). Each test file defines run_tests() and uses
// TEST_MAIN().
#include <cstdio>
#include <sstream>
#include <string>

namespace minidb_test {
inline int& failures() {
  static int f = 0;
  return f;
}
inline void report(bool ok, const char* expr, const char* file, int line) {
  if (!ok) {
    std::printf("FAIL %s:%d  %s\n", file, line, expr);
    ++failures();
  }
}
inline int summary() {
  if (failures() == 0)
    std::printf("ALL CHECKS PASSED\n");
  else
    std::printf("%d CHECK(S) FAILED\n", failures());
  return failures() == 0 ? 0 : 1;
}
}  // namespace minidb_test

#define CHECK(cond) ::minidb_test::report((cond), #cond, __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                      \
  do {                                                                      \
    auto _va = (a);                                                         \
    auto _vb = (b);                                                         \
    bool _ok = (_va == _vb);                                                \
    if (!_ok) {                                                             \
      std::ostringstream _o;                                                \
      _o << #a " == " #b " (" << _va << " vs " << _vb << ")";               \
      ::minidb_test::report(false, _o.str().c_str(), __FILE__, __LINE__);   \
    }                                                                       \
  } while (0)

#define CHECK_THROWS(expr)                                                  \
  do {                                                                      \
    bool _t = false;                                                        \
    try {                                                                   \
      (expr);                                                               \
    } catch (...) {                                                         \
      _t = true;                                                            \
    }                                                                       \
    ::minidb_test::report(_t, "throws: " #expr, __FILE__, __LINE__);        \
  } while (0)

#define TEST_MAIN()              \
  int main() {                   \
    run_tests();                 \
    return ::minidb_test::summary(); \
  }
