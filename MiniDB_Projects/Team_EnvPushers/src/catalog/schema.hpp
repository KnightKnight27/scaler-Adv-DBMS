// Table schema: an ordered list of typed, named columns, plus optional
// primary-key designation. Serialization of tuples is driven by this schema.
#pragma once

#include <string>
#include <vector>

#include "common/types.hpp"

namespace minidb {

struct Column {
    std::string name;
    TypeId      type;
    bool        is_primary_key = false;
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {}

    const std::vector<Column>& columns() const { return columns_; }
    size_t size() const { return columns_.size(); }
    const Column& column(size_t i) const { return columns_[i]; }

    // Returns column index by name, or -1 if not found.
    int index_of(const std::string& name) const {
        for (size_t i = 0; i < columns_.size(); ++i)
            if (columns_[i].name == name) return static_cast<int>(i);
        return -1;
    }

    // Index of the primary-key column, or -1 if none.
    int primary_key_index() const {
        for (size_t i = 0; i < columns_.size(); ++i)
            if (columns_[i].is_primary_key) return static_cast<int>(i);
        return -1;
    }

private:
    std::vector<Column> columns_;
};

}  // namespace minidb
