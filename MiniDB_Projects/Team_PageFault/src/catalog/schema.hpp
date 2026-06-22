#pragma once
#include <string>
#include <vector>
#include "../common/value.hpp"

namespace minidb {

struct Column {
    std::string name;
    Type type;
};

// Describes one table's columns and (de)serializes its rows to/from the byte
// strings stored in heap pages. By convention column 0 is the integer primary
// key.
class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : cols_(std::move(cols)) {}

    const std::vector<Column>& columns() const { return cols_; }
    size_t size() const { return cols_.size(); }
    const Column& column(size_t i) const { return cols_[i]; }

    // Index of a column by (optionally table-qualified) name, or -1.
    int indexOf(const std::string& name) const {
        for (size_t i = 0; i < cols_.size(); i++)
            if (cols_[i].name == name) return static_cast<int>(i);
        return -1;
    }

    // Row encoding: INT -> 8 raw bytes; TEXT -> uint16 length prefix + bytes.
    std::string serialize(const std::vector<Value>& row) const;
    std::vector<Value> deserialize(const std::string& bytes) const;

    // The primary key is column 0 and must be INT.
    int64_t primaryKey(const std::vector<Value>& row) const { return row[0].i; }

private:
    std::vector<Column> cols_;
};

} // namespace minidb
