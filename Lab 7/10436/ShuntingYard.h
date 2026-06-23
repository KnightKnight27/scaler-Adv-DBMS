#pragma once
#include <string>

class ShuntingYard {
public:
    // Evaluate an infix arithmetic expression. Returns the result.
    // Supports: +  -  *  /  ^  unary minus  ()
    // Throws std::invalid_argument on malformed input.
    double evaluate(const std::string& expr);

private:
    int precedence(char op);
    bool is_right_associative(char op);
    double apply_op(double a, double b, char op);
};
