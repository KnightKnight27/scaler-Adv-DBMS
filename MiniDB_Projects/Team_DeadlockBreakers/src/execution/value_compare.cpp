#include "execution/operators.h"

namespace minidb {

static bool CompareIntegers(int64_t left, int64_t right, CmpOp op) {
    switch (op) {
        case CmpOp::EQ: return left == right;
        case CmpOp::NEQ: return left != right;
        case CmpOp::LT: return left < right;
        case CmpOp::LTE: return left <= right;
        case CmpOp::GT: return left > right;
        case CmpOp::GTE: return left >= right;
    }
    return false;
}

static bool CompareNumbers(double left, double right, CmpOp op) {
    switch (op) {
        case CmpOp::EQ: return left == right;
        case CmpOp::NEQ: return left != right;
        case CmpOp::LT: return left < right;
        case CmpOp::LTE: return left <= right;
        case CmpOp::GT: return left > right;
        case CmpOp::GTE: return left >= right;
    }
    return false;
}

static bool CompareText(const std::string& left, const std::string& right,
                        CmpOp op) {
    switch (op) {
        case CmpOp::EQ: return left == right;
        case CmpOp::NEQ: return left != right;
        case CmpOp::LT: return left < right;
        case CmpOp::LTE: return left <= right;
        case CmpOp::GT: return left > right;
        case CmpOp::GTE: return left >= right;
    }
    return false;
}

bool CompareValues(const Value& left, const Value& right, CmpOp op) {
    if (std::holds_alternative<int64_t>(left) &&
        std::holds_alternative<int64_t>(right)) {
        return CompareIntegers(std::get<int64_t>(left),
                               std::get<int64_t>(right), op);
    }
    if (std::holds_alternative<std::string>(left) ||
        std::holds_alternative<std::string>(right)) {
        if (!std::holds_alternative<std::string>(left) ||
            !std::holds_alternative<std::string>(right)) return op == CmpOp::NEQ;
        return CompareText(std::get<std::string>(left),
                           std::get<std::string>(right), op);
    }
    double left_number = std::holds_alternative<int64_t>(left)
        ? static_cast<double>(std::get<int64_t>(left)) : std::get<double>(left);
    double right_number = std::holds_alternative<int64_t>(right)
        ? static_cast<double>(std::get<int64_t>(right)) : std::get<double>(right);
    return CompareNumbers(left_number, right_number, op);
}

} // namespace minidb
