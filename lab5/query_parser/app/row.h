#pragma once

#include <string>
#include <unordered_map>
#include <variant>

using Value = std::variant<int, std::string>;

struct Row {
    std::unordered_map<std::string, Value> columns;
};