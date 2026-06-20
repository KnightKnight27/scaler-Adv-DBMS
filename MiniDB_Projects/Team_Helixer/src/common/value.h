#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>

namespace minidb {

// MiniDB supports two column types: 32-bit signed integers and variable
// length strings. A Value is a tagged union of these. Keeping the type system
// tiny lets us focus on the *engine* rather than on type coercion rules.
enum class TypeId { INVALID, INTEGER, VARCHAR };

inline const char *TypeToString(TypeId t) {
    switch (t) {
        case TypeId::INTEGER: return "INT";
        case TypeId::VARCHAR: return "VARCHAR";
        default:              return "INVALID";
    }
}

class Value {
public:
    Value() : type_(TypeId::INVALID) {}
    explicit Value(int32_t v) : type_(TypeId::INTEGER), int_(v) {}
    explicit Value(std::string v) : type_(TypeId::VARCHAR), str_(std::move(v)) {}

    TypeId type() const { return type_; }
    bool   is_null() const { return type_ == TypeId::INVALID; }

    int32_t as_int() const {
        if (type_ != TypeId::INTEGER) throw std::runtime_error("Value is not INTEGER");
        return int_;
    }
    const std::string &as_string() const {
        if (type_ != TypeId::VARCHAR) throw std::runtime_error("Value is not VARCHAR");
        return str_;
    }

    // Three-way comparison used by predicates, ORDER, and the B+Tree.
    // Returns <0, 0, >0. Only values of the same type may be compared.
    int compare(const Value &o) const {
        if (type_ != o.type_) throw std::runtime_error("type mismatch in compare");
        if (type_ == TypeId::INTEGER) {
            return (int_ < o.int_) ? -1 : (int_ > o.int_ ? 1 : 0);
        }
        return str_.compare(o.str_) < 0 ? -1 : (str_.compare(o.str_) > 0 ? 1 : 0);
    }
    bool operator==(const Value &o) const { return type_ == o.type_ && compare(o) == 0; }
    bool operator<(const Value &o) const  { return compare(o) < 0; }

    std::string to_string() const {
        if (type_ == TypeId::INTEGER) return std::to_string(int_);
        if (type_ == TypeId::VARCHAR) return str_;
        return "NULL";
    }

private:
    TypeId      type_;
    int32_t     int_{0};
    std::string str_;
};

// A tuple is simply an ordered list of column values.
using Tuple = std::vector<Value>;

} // namespace minidb
