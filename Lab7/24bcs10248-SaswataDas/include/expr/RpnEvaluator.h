#pragma once

#include <functional>
#include <unordered_map>

#include "Interfaces.h"
#include "OperatorTable.h"

namespace expr {

// Evaluates a Reverse Polish Notation token stream to a numeric result.
// Operator semantics live in lookup tables of callables, so a new operator
// is added by registering a function rather than extending a switch.
class RpnEvaluator : public IRpnEvaluator {
public:
    RpnEvaluator();
    explicit RpnEvaluator(OperatorTable table);

    double evaluate(const std::vector<Token>& postfix) const override;

private:
    OperatorTable operators_;
    std::unordered_map<char, std::function<double(double, double)>> binaryOps_;
    std::unordered_map<char, std::function<double(double)>> unaryOps_;
};

}  // namespace expr
