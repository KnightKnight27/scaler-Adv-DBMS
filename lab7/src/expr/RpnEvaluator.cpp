#include "expr/RpnEvaluator.h"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace expr {

RpnEvaluator::RpnEvaluator() : RpnEvaluator(OperatorTable{}) {}

RpnEvaluator::RpnEvaluator(OperatorTable table) : operators_(std::move(table)) {
    binaryOps_['+'] = [](double a, double b) { return a + b; };
    binaryOps_['-'] = [](double a, double b) { return a - b; };
    binaryOps_['*'] = [](double a, double b) { return a * b; };
    binaryOps_['/'] = [](double a, double b) {
        if (b == 0.0) throw std::runtime_error("division by zero");
        return a / b;
    };
    binaryOps_['%'] = [](double a, double b) {
        if (b == 0.0) throw std::runtime_error("modulo by zero");
        return std::fmod(a, b);
    };
    binaryOps_['^'] = [](double a, double b) { return std::pow(a, b); };

    unaryOps_[OperatorTable::kUnaryMinus] = [](double a) { return -a; };
    unaryOps_[OperatorTable::kUnaryPlus] = [](double a) { return a; };
}

double RpnEvaluator::evaluate(const std::vector<Token>& postfix) const {
    std::vector<double> stack;

    for (const Token& token : postfix) {
        if (token.type == TokenType::Number) {
            stack.push_back(token.number);
            continue;
        }
        if (token.type != TokenType::Operator) {
            throw std::runtime_error("unexpected token in RPN stream");
        }

        const OperatorInfo& meta = operators_.info(token.op);
        if (meta.unary) {
            if (stack.empty()) throw std::runtime_error("missing operand for unary operator");
            double a = stack.back();
            stack.pop_back();
            stack.push_back(unaryOps_.at(token.op)(a));
        } else {
            if (stack.size() < 2) throw std::runtime_error("missing operand for binary operator");
            double b = stack.back();
            stack.pop_back();
            double a = stack.back();
            stack.pop_back();
            stack.push_back(binaryOps_.at(token.op)(a, b));
        }
    }

    if (stack.size() != 1) {
        throw std::runtime_error("invalid expression: operand/operator count mismatch");
    }
    return stack.back();
}

}  // namespace expr
