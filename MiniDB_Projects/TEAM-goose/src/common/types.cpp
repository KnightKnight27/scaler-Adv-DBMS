#include "types.h"

namespace minidb {

bool Value::operator==(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
        case ValueType::INT32:   return int_val == o.int_val;
        case ValueType::FLOAT64: return float_val == o.float_val;
        case ValueType::STRING:  return str_val == o.str_val;
        case ValueType::NONE:    return true;
    }
    return false;
}

bool Value::operator<(const Value& o) const {
    if (type != o.type) return static_cast<int>(type) < static_cast<int>(o.type);
    switch (type) {
        case ValueType::INT32:   return int_val < o.int_val;
        case ValueType::FLOAT64: return float_val < o.float_val;
        case ValueType::STRING:  return str_val < o.str_val;
        case ValueType::NONE:    return false;
    }
    return false;
}

std::string Value::to_string() const {
    switch (type) {
        case ValueType::INT32:   return std::to_string(int_val);
        case ValueType::FLOAT64: return std::to_string(float_val);
        case ValueType::STRING:  return str_val;
        case ValueType::NONE:    return "NULL";
    }
    return "?";
}

} // namespace minidb
