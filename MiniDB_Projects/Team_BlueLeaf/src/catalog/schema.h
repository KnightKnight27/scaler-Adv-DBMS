#pragma once

#include <string>
#include <vector>

#include "common/types.h"

namespace minidb {

// One column of a table.
struct Column {
    std::string name;
    ValueType   type;
    // For VARCHAR this is the declared maximum length; for INT/DOUBLE it is 8.
    std::uint16_t length = 8;
};

// A table's schema: an ordered list of columns. Records are encoded/decoded
// against a Schema (see Record).
class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : cols_(std::move(cols)) {}

    const std::vector<Column>& columns() const { return cols_; }
    std::size_t num_columns() const { return cols_.size(); }
    const Column& column(int i) const { return cols_[static_cast<std::size_t>(i)]; }

    // Index of a column by name, or -1 if absent.
    int index_of(const std::string& name) const {
        for (std::size_t i = 0; i < cols_.size(); ++i)
            if (cols_[i].name == name) return static_cast<int>(i);
        return -1;
    }

private:
    std::vector<Column> cols_;
};

} // namespace minidb
