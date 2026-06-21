// Shared primitive types used across the whole engine.
//
// Design choices kept deliberately simple:
//   * A Value is either an INT (int64) or TEXT (string), with a tag saying
//     which. A Row is just a vector<Value>.
//   * The Catalog remembers the Schema of every table, so the rest of the
//     engine always knows how many columns a row has and what type each is.
//   * The first column of a table is its PRIMARY KEY and is what the B+ tree
//     indexes. We support INT primary keys.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

enum class Type { INT, TEXT };

// A single column value: an integer or a piece of text.
struct Value {
    Type type = Type::INT;
    int64_t i = 0;
    std::string s;

    static Value Int(int64_t v) { Value x; x.type = Type::INT; x.i = v; return x; }
    static Value Text(std::string v) { Value x; x.type = Type::TEXT; x.s = std::move(v); return x; }

    bool isInt() const { return type == Type::INT; }
    std::string toString() const { return isInt() ? std::to_string(i) : s; }

    bool operator==(const Value& o) const {
        if (type != o.type) return false;
        return isInt() ? (i == o.i) : (s == o.s);
    }
};

using Row = std::vector<Value>;

struct Column {
    std::string name;
    Type type;
};

struct Schema {
    std::vector<Column> columns;

    size_t size() const { return columns.size(); }

    int indexOf(const std::string& name) const {
        for (size_t i = 0; i < columns.size(); ++i)
            if (columns[i].name == name) return static_cast<int>(i);
        return -1;
    }
};

// Record identifier: the physical address of a row (which page, which slot).
struct RID {
    int32_t page = -1;
    int32_t slot = -1;
    bool valid() const { return page >= 0; }
};

}  // namespace minidb
