#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "common/value.h"

namespace minidb {

// A column is just a name and a type. (No NULL/constraints beyond the primary
// key, deliberately, to keep the type system minimal.)
struct Column {
    std::string name;
    TypeId      type;
};

// A schema describes one table: its ordered columns and which column (if any)
// is the primary key. The primary key column is the one indexed by the B+Tree.
struct Schema {
    std::vector<Column> columns;
    int                 pk_index{-1}; // -1 => no primary key

    int column_count() const { return static_cast<int>(columns.size()); }

    // Resolve a column name to its position (or -1 if absent).
    int index_of(const std::string &name) const {
        for (int i = 0; i < column_count(); ++i)
            if (columns[i].name == name) return i;
        return -1;
    }
    TypeId type_of(int i) const { return columns[i].type; }
};

// ---- Tuple serialization -------------------------------------------------
// Records are variable length. Layout follows the schema column order:
//   INTEGER -> 4 raw bytes
//   VARCHAR -> 4-byte length prefix, then the characters
// Because the schema fixes the column types, the serialized bytes carry no
// per-value type tags, which keeps records compact.

inline std::vector<char> SerializeTuple(const Tuple &t, const Schema &schema) {
    if ((int)t.size() != schema.column_count())
        throw std::runtime_error("tuple/schema arity mismatch");
    std::vector<char> buf;
    for (int i = 0; i < schema.column_count(); ++i) {
        if (schema.type_of(i) == TypeId::INTEGER) {
            int32_t v = t[i].as_int();
            const char *p = reinterpret_cast<const char *>(&v);
            buf.insert(buf.end(), p, p + sizeof(int32_t));
        } else { // VARCHAR
            const std::string &s = t[i].as_string();
            int32_t len = static_cast<int32_t>(s.size());
            const char *p = reinterpret_cast<const char *>(&len);
            buf.insert(buf.end(), p, p + sizeof(int32_t));
            buf.insert(buf.end(), s.begin(), s.end());
        }
    }
    return buf;
}

inline Tuple DeserializeTuple(const char *data, const Schema &schema) {
    Tuple t;
    size_t off = 0;
    for (int i = 0; i < schema.column_count(); ++i) {
        if (schema.type_of(i) == TypeId::INTEGER) {
            int32_t v;
            std::memcpy(&v, data + off, sizeof(int32_t));
            off += sizeof(int32_t);
            t.emplace_back(v);
        } else { // VARCHAR
            int32_t len;
            std::memcpy(&len, data + off, sizeof(int32_t));
            off += sizeof(int32_t);
            t.emplace_back(std::string(data + off, data + off + len));
            off += len;
        }
    }
    return t;
}

} // namespace minidb
