#pragma once

#include <unordered_map>

namespace expr {

enum class Associativity { Left, Right };

struct OperatorInfo {
    int precedence;
    Associativity associativity;
    bool unary;  // true for prefix operators such as unary minus
};

// Single source of truth for operator metadata. Both the Shunting-Yard
// converter and the RPN evaluator consult this table, so adding a new
// operator is a registration change here rather than an edit to the
// algorithms (Open/Closed Principle).
//
// Internal symbols are used for the prefix operators so they never clash
// with their binary counterparts:
//   'm' -> unary minus, 'p' -> unary plus.
class OperatorTable {
public:
    OperatorTable();

    void add(char symbol, int precedence, Associativity associativity, bool unary);

    bool isOperator(char symbol) const;
    const OperatorInfo& info(char symbol) const;  // throws if symbol is unknown

    static constexpr char kUnaryMinus = 'm';
    static constexpr char kUnaryPlus = 'p';

private:
    std::unordered_map<char, OperatorInfo> operators_;
};

}  // namespace expr
