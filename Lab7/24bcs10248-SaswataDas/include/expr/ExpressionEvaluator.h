#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Interfaces.h"

namespace expr {

// Facade that wires the three pipeline stages together:
//     string -> tokenize -> infix-to-postfix -> evaluate -> double
//
// The default constructor assembles the standard concrete stages. The
// injecting constructor accepts any implementations of the interfaces,
// which keeps the facade open for substitution and easy to unit test.
class ExpressionEvaluator {
public:
    ExpressionEvaluator();
    ExpressionEvaluator(std::unique_ptr<ITokenizer> tokenizer,
                        std::unique_ptr<IInfixToPostfix> converter,
                        std::unique_ptr<IRpnEvaluator> evaluator);

    double evaluate(const std::string& expression) const;

    // Exposes the intermediate RPN form, primarily for demonstration.
    std::vector<Token> toPostfix(const std::string& expression) const;

private:
    std::unique_ptr<ITokenizer> tokenizer_;
    std::unique_ptr<IInfixToPostfix> converter_;
    std::unique_ptr<IRpnEvaluator> evaluator_;
};

}  // namespace expr
