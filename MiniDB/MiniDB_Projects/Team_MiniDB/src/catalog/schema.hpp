#pragma once

#include <string>
#include <vector>

#include "../common/value.hpp"

// A table's column definition.
struct Column {
    std::string name;
    ColumnType  type;
};

// A Schema is the ordered list of columns. Because every row in a table shares
// the same schema, rows are stored WITHOUT column names — just the values in
// schema order. The schema is what turns those raw bytes back into a row.
struct Schema {
    std::vector<Column> columns;

    // Index of a column by name, or -1 if absent.
    int index_of(const std::string& name) const;

    // Encode a row (values in column order) into the byte string the heap stores.
    //   INT  -> 4 raw bytes
    //   TEXT -> 2-byte length prefix + the characters
    std::string serialize(const std::vector<Value>& row) const;

    // Decode bytes produced by serialize() back into a row, using this schema.
    std::vector<Value> deserialize(const std::string& bytes) const;
};
