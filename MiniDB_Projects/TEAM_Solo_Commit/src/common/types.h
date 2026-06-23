// MiniDB - common typed-value model
// A Value is a typed scalar: INTEGER (int32), BIGINT (int64), VARCHAR (string), BOOLEAN.
// Numeric kinds share an int64 slot; VARCHAR uses the string slot. This keeps comparison
// and serialization simple, which is all a teaching engine needs.
#pragma once

#include <cstdint>
#include <string>

namespace minidb {

enum class TypeId { INVALID, INTEGER, BIGINT, VARCHAR, BOOLEAN };

const char* TypeIdToString(TypeId t);
TypeId TypeIdFromString(const std::string& s);

class Value {
public:
    Value() : type_(TypeId::INVALID), num_(0) {}
    static Value MakeInt(int32_t v)       { Value x; x.type_ = TypeId::INTEGER; x.num_ = v;        return x; }
    static Value MakeBigInt(int64_t v)    { Value x; x.type_ = TypeId::BIGINT;  x.num_ = v;        return x; }
    static Value MakeBool(bool v)         { Value x; x.type_ = TypeId::BOOLEAN; x.num_ = v ? 1 : 0;return x; }
    static Value MakeVarchar(std::string v){Value x; x.type_ = TypeId::VARCHAR; x.str_ = std::move(v); return x; }

    TypeId type() const { return type_; }
    bool IsNull() const { return type_ == TypeId::INVALID; }

    int32_t AsInt() const      { return static_cast<int32_t>(num_); }
    int64_t AsBigInt() const   { return num_; }
    bool    AsBool() const     { return num_ != 0; }
    const std::string& AsString() const { return str_; }

    bool IsNumeric() const {
        return type_ == TypeId::INTEGER || type_ == TypeId::BIGINT || type_ == TypeId::BOOLEAN;
    }

    // Three-way compare. Numeric kinds compare by their int64 slot; VARCHAR by string.
    // Comparing across the numeric/string divide is treated as ordering by type id.
    int Compare(const Value& o) const;

    bool operator==(const Value& o) const { return Compare(o) == 0; }
    bool operator!=(const Value& o) const { return Compare(o) != 0; }
    bool operator< (const Value& o) const { return Compare(o) <  0; }
    bool operator<=(const Value& o) const { return Compare(o) <= 0; }
    bool operator> (const Value& o) const { return Compare(o) >  0; }
    bool operator>=(const Value& o) const { return Compare(o) >= 0; }

    std::string ToString() const;

private:
    TypeId type_;
    int64_t num_;
    std::string str_;
};

}  // namespace minidb
