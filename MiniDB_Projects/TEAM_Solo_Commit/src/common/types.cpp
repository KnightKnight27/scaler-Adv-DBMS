#include "types.h"

namespace minidb {

const char* TypeIdToString(TypeId t) {
    switch (t) {
        case TypeId::INTEGER: return "INTEGER";
        case TypeId::BIGINT:  return "BIGINT";
        case TypeId::VARCHAR: return "VARCHAR";
        case TypeId::BOOLEAN: return "BOOLEAN";
        default:              return "INVALID";
    }
}

TypeId TypeIdFromString(const std::string& s) {
    if (s == "INT" || s == "INTEGER") return TypeId::INTEGER;
    if (s == "BIGINT")                return TypeId::BIGINT;
    if (s == "VARCHAR" || s == "TEXT" || s == "STRING") return TypeId::VARCHAR;
    if (s == "BOOL" || s == "BOOLEAN") return TypeId::BOOLEAN;
    return TypeId::INVALID;
}

int Value::Compare(const Value& o) const {
    // Order by category first: all numeric kinds together, then varchar.
    bool an = IsNumeric(), bn = o.IsNumeric();
    if (an != bn) return an ? -1 : 1;
    if (an) {
        if (num_ < o.num_) return -1;
        if (num_ > o.num_) return 1;
        return 0;
    }
    return str_.compare(o.str_) < 0 ? -1 : (str_ == o.str_ ? 0 : 1);
}

std::string Value::ToString() const {
    switch (type_) {
        case TypeId::INTEGER: return std::to_string(static_cast<int32_t>(num_));
        case TypeId::BIGINT:  return std::to_string(num_);
        case TypeId::BOOLEAN: return num_ ? "true" : "false";
        case TypeId::VARCHAR: return str_;
        default:              return "NULL";
    }
}

}  // namespace minidb
