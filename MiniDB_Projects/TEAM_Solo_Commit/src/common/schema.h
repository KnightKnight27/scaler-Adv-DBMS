// MiniDB - table schema: an ordered list of typed columns, with optional primary-key marking.
#pragma once

#include <string>
#include <vector>
#include "types.h"

namespace minidb {

struct Column {
    std::string name;
    TypeId type = TypeId::INVALID;
    bool is_primary_key = false;
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : cols_(std::move(cols)) {}

    size_t Count() const { return cols_.size(); }
    const Column& GetColumn(size_t i) const { return cols_[i]; }
    const std::vector<Column>& Columns() const { return cols_; }

    // Returns the column index for `name`, or -1 if absent. Matching tolerates
    // table qualification on either side: a query ref "t.col" or bare "col" both
    // resolve against a stored column named "t.col" or "col". After a join the
    // stored names are fully qualified ("students.id"); single-table schemas use
    // bare names. First match wins (ambiguous bare names in joins are a known limit).
    int GetColIdx(const std::string& name) const {
        auto bare = [](const std::string& s) {
            auto dot = s.find('.');
            return dot == std::string::npos ? s : s.substr(dot + 1);
        };
        std::string qbare = bare(name);
        for (size_t i = 0; i < cols_.size(); ++i) {
            const std::string& cn = cols_[i].name;
            if (cn == name || cn == qbare || bare(cn) == name || bare(cn) == qbare)
                return static_cast<int>(i);
        }
        return -1;
    }

    int PrimaryKeyIdx() const {
        for (size_t i = 0; i < cols_.size(); ++i)
            if (cols_[i].is_primary_key) return static_cast<int>(i);
        return -1;
    }

private:
    std::vector<Column> cols_;
};

}  // namespace minidb
