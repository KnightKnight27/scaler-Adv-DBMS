// Core value / type definitions shared across every layer of MiniDB.
//
// A MiniDB column is one of two SQL types: INT (stored as int64) or TEXT
// (variable-length UTF-8).  A `Value` is a tagged union over those plus SQL
// NULL.  Rows (`Tuple`) are vectors of `Value`.  Records on disk are addressed
// by `RID` = (page_id, slot_id).
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <stdexcept>

namespace minidb {

using PageId = int64_t;
using SlotId = uint16_t;
using TxnId  = int64_t;
using lsn_t  = int64_t;  // log sequence number

constexpr PageId INVALID_PAGE_ID = -1;

enum class TypeId { INTEGER, TEXT };

inline std::string type_name(TypeId t) {
    switch (t) {
        case TypeId::INTEGER: return "INT";
        case TypeId::TEXT:    return "TEXT";
    }
    return "?";
}

// Record identifier: where a tuple physically lives in a heap file.
struct RID {
    PageId page_id = INVALID_PAGE_ID;
    SlotId slot_id = 0;

    bool operator==(const RID& o) const {
        return page_id == o.page_id && slot_id == o.slot_id;
    }
    bool valid() const { return page_id != INVALID_PAGE_ID; }
};

// A single SQL value: NULL, an integer, or a string.
class Value {
public:
    Value() : data_(std::monostate{}) {}                       // NULL
    explicit Value(int64_t v) : data_(v) {}
    explicit Value(std::string v) : data_(std::move(v)) {}

    static Value Null() { return Value(); }
    static Value Int(int64_t v) { return Value(v); }
    static Value Text(std::string v) { return Value(std::move(v)); }

    bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
    bool is_int()  const { return std::holds_alternative<int64_t>(data_); }
    bool is_text() const { return std::holds_alternative<std::string>(data_); }

    int64_t as_int() const {
        if (!is_int()) throw std::runtime_error("Value is not INT");
        return std::get<int64_t>(data_);
    }
    const std::string& as_text() const {
        if (!is_text()) throw std::runtime_error("Value is not TEXT");
        return std::get<std::string>(data_);
    }

    TypeId type() const {
        if (is_int()) return TypeId::INTEGER;
        return TypeId::TEXT;  // NULL treated as TEXT-compatible for display
    }

    std::string to_string() const {
        if (is_null()) return "NULL";
        if (is_int())  return std::to_string(std::get<int64_t>(data_));
        return std::get<std::string>(data_);
    }

    // Total order used by comparisons / index keys. NULLs sort first.
    // Returns <0, 0, >0.
    int compare(const Value& o) const {
        if (is_null() || o.is_null()) {
            return (is_null() ? 0 : 1) - (o.is_null() ? 0 : 1);
        }
        if (is_int() && o.is_int()) {
            int64_t a = as_int(), b = o.as_int();
            return (a < b) ? -1 : (a > b ? 1 : 0);
        }
        const std::string& a = as_text();
        const std::string& b = o.as_text();
        return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
    }

    bool operator==(const Value& o) const { return compare(o) == 0; }
    bool operator<(const Value& o)  const { return compare(o) < 0; }

private:
    std::variant<std::monostate, int64_t, std::string> data_;
};

using Tuple = std::vector<Value>;

}  // namespace minidb
