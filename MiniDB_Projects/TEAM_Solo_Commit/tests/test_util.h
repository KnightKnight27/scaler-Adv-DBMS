// MiniDB - tiny test helper. Each test is its own executable registered with CTest; CHECK
// records a failure and the program returns non-zero if anything failed.
#pragma once

#include <cstdio>
#include <string>

namespace minidb_test {
inline int& failures() { static int f = 0; return f; }

inline void Check(bool cond, const std::string& what) {
    if (cond) {
        printf("  ok   : %s\n", what.c_str());
    } else {
        printf("  FAIL : %s\n", what.c_str());
        ++failures();
    }
}
inline int Done(const char* name) {
    printf("[%s] %s\n", name, failures() == 0 ? "PASSED" : "FAILED");
    return failures() == 0 ? 0 : 1;
}
}  // namespace minidb_test

#define CHECK(cond) minidb_test::Check((cond), #cond)
