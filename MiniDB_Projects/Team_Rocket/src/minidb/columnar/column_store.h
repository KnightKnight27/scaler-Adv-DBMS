#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../types.h"

namespace minidb {

// Track A: a read-optimised columnar mirror of a table. Each column's values
// are stored contiguously, so an analytical query touches only the columns it
// needs instead of decoding whole rows.
class ColumnStore {
public:
    explicit ColumnStore(const Schema& schema) : schema_(schema), int_cols_(schema.size()) {}

    void append(const Tuple& t) {
        for (size_t c = 0; c < schema_.size(); ++c)
            if (schema_[c].type == Type::Int) int_cols_[c].push_back(t[c].i);
        ++rows_;
    }

    int column(const std::string& name) const { return column_index(schema_, name); }
    const std::vector<int64_t>& ints(int col) const { return int_cols_[col]; }
    size_t rows() const { return rows_; }

private:
    Schema schema_;
    std::vector<std::vector<int64_t>> int_cols_;
    size_t rows_ = 0;
};

}  // namespace minidb
