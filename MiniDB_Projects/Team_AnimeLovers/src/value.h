#pragma once
// value.h — The two primitive types MiniDB understands: INT and VARCHAR.
//
// Every row, index key, and query predicate is built from Value.
// We use std::variant so the compiler enforces that we always check
// the active type before reading the payload (no silent UB from bare unions).
#include <string>
#include <variant>

namespace minidb {

enum class Type { INT, VARCHAR };

struct Value {
    Type type;
    std::variant<int, std::string> data;

    // Named constructors make call-sites self-documenting.
    static Value make_int(int v)           { return {Type::INT,     v}; }
    static Value make_str(std::string v)   { return {Type::VARCHAR, std::move(v)}; }

    int               as_int() const       { return std::get<int>(data); }
    const std::string& as_str() const      { return std::get<std::string>(data); }

    std::string to_string() const {
        if (type == Type::INT) return std::to_string(as_int());
        return as_str();
    }

    bool operator==(const Value& o) const { return type == o.type && data == o.data; }
    bool operator!=(const Value& o) const { return !(*this == o); }
    bool operator< (const Value& o) const {
        if (type == Type::INT) return as_int() < o.as_int();
        return as_str() < o.as_str();
    }
    bool operator<=(const Value& o) const { return !(o < *this); }
    bool operator> (const Value& o) const { return  (o < *this); }
    bool operator>=(const Value& o) const { return !(*this < o); }
};

// A Record ID uniquely identifies a row on disk: which page it lives on
// and which slot within that page.  The B+ Tree stores RIDs as its leaf payload.
struct RID {
    int page_id = -1;
    int slot    = -1;
    bool valid() const { return page_id >= 0 && slot >= 0; }
};

} // namespace minidb
