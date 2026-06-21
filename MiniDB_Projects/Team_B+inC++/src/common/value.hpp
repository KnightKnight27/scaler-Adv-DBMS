#pragma once

#include <stdexcept>
#include <string>
#include <variant>

// column value: int or text
using Value = std::variant<int, std::string>;

enum class ColumnType { INT, TEXT };

inline std::string value_to_string(const Value& v) {
    if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    return std::get<std::string>(v);
}

// 3-way compare, same type only; throws on mismatch
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
