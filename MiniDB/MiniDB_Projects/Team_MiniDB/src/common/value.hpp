#pragma once

#include <stdexcept>
#include <string>
#include <variant>

// A single column value. We support exactly two column types (INT, TEXT) — the
// minimum the rubric needs — so a Value is either a 32-bit int or a string.
using Value = std::variant<int, std::string>;

enum class ColumnType { INT, TEXT };

inline std::string value_to_string(const Value& v) {
    if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    return std::get<std::string>(v);
}

// Three-way compare of two values of the SAME type. Throws on a type mismatch
// (e.g. comparing an int column against a string literal) — that's a query bug.
inline int value_compare(const Value& a, const Value& b) {
    if (a.index() != b.index())
        throw std::runtime_error("type mismatch in comparison");
    if (std::holds_alternative<int>(a)) {
        int x = std::get<int>(a), y = std::get<int>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    const std::string& x = std::get<std::string>(a);
    const std::string& y = std::get<std::string>(b);
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}
