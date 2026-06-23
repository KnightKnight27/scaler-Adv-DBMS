#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <iostream>

using Value = std::variant<int, std::string, bool>;

using Row = std::unordered_map<std::string, Value>;
using Table = std::vector<Row>;

inline void printValue(const Value& val) {
    if (std::holds_alternative<int>(val)) {
        std::cout << std::get<int>(val);
    } else if (std::holds_alternative<std::string>(val)) {
        std::cout << std::get<std::string>(val);
    } else if (std::holds_alternative<bool>(val)) {
        std::cout << (std::get<bool>(val) ? "TRUE" : "FALSE");
    }
}
