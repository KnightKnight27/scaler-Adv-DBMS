// ============================================================================
//  tuple.hpp — Schema + row serialization.
//
//  A Schema is the ordered list of columns for a table. A Tuple is one row:
//  a vector of Values matching the schema. The on-disk byte format is:
//
//    INT  column  ->  8 bytes, little-endian int64
//    TEXT column  ->  2-byte length prefix, then that many raw bytes
//
//  Fixed-width ints first would let us skip the length math, but mixing the
//  prefix in keeps the encoder a single linear pass and the format easy to
//  explain in the viva. Serialization is the contract between the in-memory
//  Value world and the raw bytes the heap pages store.
// ============================================================================
#pragma once

#include "../common/types.hpp"
#include "../sql/ast.hpp"   // Value, ColType, ColumnDef

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace minidb {

struct Schema {
    std::vector<ColumnDef> columns;

    int index_of(const std::string& col) const {
        // accept either "col" or "table.col" — compare on the part after '.'
        std::string want = col;
        auto dot = want.find('.');
        if (dot != std::string::npos) want = want.substr(dot + 1);
        for (int i = 0; i < (int)columns.size(); ++i)
            if (columns[i].name == want) return i;
        return -1;
    }
    size_t size() const { return columns.size(); }
};

struct Tuple {
    std::vector<Value> values;

    // Encode this tuple to bytes according to `schema`.
    std::string serialize(const Schema& schema) const {
        if (values.size() != schema.size())
            throw std::runtime_error("tuple: value count != column count");
        std::string buf;
        for (size_t c = 0; c < schema.size(); ++c) {
            const Value& v = values[c];
            if (schema.columns[c].type == ColType::INT) {
                int64_t x = v.i;
                buf.append(reinterpret_cast<const char*>(&x), sizeof(x));
            } else {  // TEXT
                if (v.s.size() > 0xFFFF) throw std::runtime_error("tuple: TEXT too long");
                uint16_t len = (uint16_t)v.s.size();
                buf.append(reinterpret_cast<const char*>(&len), sizeof(len));
                buf.append(v.s);
            }
        }
        return buf;
    }

    // Decode bytes back into a Tuple using the same schema.
    static Tuple deserialize(const char* data, const Schema& schema) {
        Tuple t;
        size_t off = 0;
        for (size_t c = 0; c < schema.size(); ++c) {
            if (schema.columns[c].type == ColType::INT) {
                int64_t x;
                std::memcpy(&x, data + off, sizeof(x));
                off += sizeof(x);
                t.values.push_back(Value::Int(x));
            } else {
                uint16_t len;
                std::memcpy(&len, data + off, sizeof(len));
                off += sizeof(len);
                t.values.push_back(Value::Text(std::string(data + off, len)));
                off += len;
            }
        }
        return t;
    }
};

// Compare a tuple's column value against a literal — the primitive the WHERE
// evaluator and the optimizer both lean on. Returns <0, 0, >0 like strcmp.
inline int compare_value(const Value& a, const Value& b) {
    if (a.type == ColType::INT) return (a.i < b.i) ? -1 : (a.i > b.i) ? 1 : 0;
    return a.s.compare(b.s);
}

}  // namespace minidb
