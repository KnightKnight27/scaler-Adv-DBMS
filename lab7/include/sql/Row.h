#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>

#include "Value.h"

namespace sql {

// A single record: a mapping from column name to value.
class Row {
public:
    void set(const std::string& column, Value value) { fields_[column] = std::move(value); }

    bool has(const std::string& column) const {
        return fields_.find(column) != fields_.end();
    }

    const Value& at(const std::string& column) const {
        auto it = fields_.find(column);
        if (it == fields_.end()) {
            throw std::runtime_error("unknown column: " + column);
        }
        return it->second;
    }

private:
    std::unordered_map<std::string, Value> fields_;
};

}  // namespace sql
