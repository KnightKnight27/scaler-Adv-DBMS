#pragma once

#include <string>
#include <vector>

#include "Token.h"

namespace expr {

// Abstractions that the ExpressionEvaluator facade depends on. Concrete
// pipeline stages implement these, and the facade is wired through the
// interfaces (Dependency Inversion + Interface Segregation: each contract
// is a single, focused operation).

struct ITokenizer {
    virtual ~ITokenizer() = default;
    virtual std::vector<Token> tokenize(const std::string& input) const = 0;
};

struct IInfixToPostfix {
    virtual ~IInfixToPostfix() = default;
    virtual std::vector<Token> toPostfix(const std::vector<Token>& infix) const = 0;
};

struct IRpnEvaluator {
    virtual ~IRpnEvaluator() = default;
    virtual double evaluate(const std::vector<Token>& postfix) const = 0;
};

}  // namespace expr
