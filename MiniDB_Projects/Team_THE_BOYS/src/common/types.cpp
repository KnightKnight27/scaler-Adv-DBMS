#include "common/types.h"

#include <stdexcept>

namespace minidb {

Value Value::Null() {
    Value v;
    v.type = ValueType::NULL_TYPE;
    v.data = std::monostate{};
    return v;
}

Value Value::Int(int64_t val) {
    Value v;
    v.type = ValueType::INT;
    v.data = val;
    return v;
}

Value Value::Str(std::string val) {
    Value v;
    v.type = ValueType::STRING;
    v.data = std::move(val);
    return v;
}

bool Value::operator==(const Value& other) const {
    if (type != other.type) {
        return false;
    }
    if (type == ValueType::NULL_TYPE) {
        return true;
    }
    if (type == ValueType::INT) {
        return std::get<int64_t>(data) == std::get<int64_t>(other.data);
    }
    return std::get<std::string>(data) == std::get<std::string>(other.data);
}

bool Value::operator<(const Value& other) const {
    if (type == ValueType::NULL_TYPE || other.type == ValueType::NULL_TYPE) {
        return false;
    }
    if (type != other.type) {
        throw std::runtime_error("Type mismatch in comparison");
    }
    if (type == ValueType::INT) {
        return std::get<int64_t>(data) < std::get<int64_t>(other.data);
    }
    return std::get<std::string>(data) < std::get<std::string>(other.data);
}

std::string Value::ToString() const {
    if (type == ValueType::NULL_TYPE) {
        return "NULL";
    }
    if (type == ValueType::INT) {
        return std::to_string(std::get<int64_t>(data));
    }
    return "'" + std::get<std::string>(data) + "'";
}

bool CompareValues(const Value& lhs, CompareOp op, const Value& rhs) {
    switch (op) {
        case CompareOp::EQ:
            return lhs == rhs;
        case CompareOp::NE:
            return lhs != rhs;
        case CompareOp::LT:
            return lhs < rhs;
        case CompareOp::LE:
            return lhs < rhs || lhs == rhs;
        case CompareOp::GT:
            return rhs < lhs;
        case CompareOp::GE:
            return rhs < lhs || lhs == rhs;
    }
    return false;
}

CompareOp ParseCompareOp(const std::string& token) {
    if (token == "=" || token == "==") return CompareOp::EQ;
    if (token == "!=" || token == "<>") return CompareOp::NE;
    if (token == "<") return CompareOp::LT;
    if (token == "<=") return CompareOp::LE;
    if (token == ">") return CompareOp::GT;
    if (token == ">=") return CompareOp::GE;
    throw std::runtime_error("Unknown comparison operator: " + token);
}

}  // namespace minidb
