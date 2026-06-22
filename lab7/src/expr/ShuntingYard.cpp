#include "expr/ShuntingYard.h"

#include <stdexcept>
#include <vector>

namespace expr {

ShuntingYard::ShuntingYard(OperatorTable table) : operators_(std::move(table)) {}

std::vector<Token> ShuntingYard::toPostfix(const std::vector<Token>& infix) const {
    std::vector<Token> output;
    std::vector<Token> opStack;

    for (const Token& token : infix) {
        switch (token.type) {
            case TokenType::Number:
                output.push_back(token);
                break;

            case TokenType::Operator: {
                const OperatorInfo& o1 = operators_.info(token.op);
                while (!opStack.empty() && opStack.back().type == TokenType::Operator) {
                    const OperatorInfo& o2 = operators_.info(opStack.back().op);
                    const bool popForLeft =
                        o1.associativity == Associativity::Left && o2.precedence >= o1.precedence;
                    const bool popForRight =
                        o1.associativity == Associativity::Right && o2.precedence > o1.precedence;
                    if (popForLeft || popForRight) {
                        output.push_back(opStack.back());
                        opStack.pop_back();
                    } else {
                        break;
                    }
                }
                opStack.push_back(token);
                break;
            }

            case TokenType::LeftParen:
                opStack.push_back(token);
                break;

            case TokenType::RightParen: {
                bool foundLeftParen = false;
                while (!opStack.empty()) {
                    if (opStack.back().type == TokenType::LeftParen) {
                        foundLeftParen = true;
                        opStack.pop_back();
                        break;
                    }
                    output.push_back(opStack.back());
                    opStack.pop_back();
                }
                if (!foundLeftParen) {
                    throw std::runtime_error("mismatched parentheses: missing '('");
                }
                break;
            }
        }
    }

    while (!opStack.empty()) {
        if (opStack.back().type == TokenType::LeftParen) {
            throw std::runtime_error("mismatched parentheses: missing ')'");
        }
        output.push_back(opStack.back());
        opStack.pop_back();
    }

    return output;
}

}  // namespace expr
