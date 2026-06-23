#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace minidb {

// A simplified Tuple implementation.
// For the capstone, we assume a fixed schema.
// Example: { int32_t id; char name[32]; }
// To keep it generic but simple, we can just hold a fixed size byte array.
constexpr size_t TUPLE_SIZE = 36; // 4 bytes for int + 32 bytes for a string

struct Tuple {
    int32_t id;
    char name[32];

    Tuple() : id(0) {
        name[0] = '\0';
    }

    Tuple(int32_t id, const char* n) : id(id) {
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0'; // ensure null termination
    }
};

} // namespace minidb
