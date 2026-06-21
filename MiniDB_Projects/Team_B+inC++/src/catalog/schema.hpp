#pragma once

#include <string>
#include <vector>

#include "../common/value.hpp"

struct Column {
    std::string name;
    ColumnType  type;
};

// ordered columns; rows stored as values in schema order
struct Schema {
    std::vector<Column> columns;

    // -1 if absent
    int index_of(const std::string& name) const;

    // INT -> 4 bytes, TEXT -> 2-byte len + chars
    std::string serialize(const std::vector<Value>& row) const;

    std::vector<Value> deserialize(const std::string& bytes) const;
};
