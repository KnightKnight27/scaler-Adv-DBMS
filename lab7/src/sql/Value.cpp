#include "sql/Value.h"

#include <sstream>
#include <stdexcept>

namespace sql {

namespace {

bool bothNumbers(const Value& a, const Value& b) {
    return std::holds_alternative<double>(a) && std::holds_alternative<double>(b);
}

bool bothStrings(const Value& a, const Value& b) {
    return std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b);
}

}  // namespace

bool valueEqual(const Value& lhs, const Value& rhs) {
    if (bothNumbers(lhs, rhs)) {
        return std::get<double>(lhs) == std::get<double>(rhs);
    }
    if (bothStrings(lhs, rhs)) {
        return std::get<std::string>(lhs) == std::get<std::string>(rhs);
    }
    throw std::runtime_error("type mismatch: cannot compare number with text");
}

bool valueLess(const Value& lhs, const Value& rhs) {
    if (bothNumbers(lhs, rhs)) {
        return std::get<double>(lhs) < std::get<double>(rhs);
    }
    if (bothStrings(lhs, rhs)) {
        return std::get<std::string>(lhs) < std::get<std::string>(rhs);
    }
    throw std::runtime_error("type mismatch: cannot compare number with text");
}

std::string toString(const Value& value) {
    if (std::holds_alternative<double>(value)) {
        std::ostringstream out;
        out << std::get<double>(value);
        return out.str();
    }
    return std::get<std::string>(value);
}

}  // namespace sql
