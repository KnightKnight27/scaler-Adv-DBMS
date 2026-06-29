#include "expr/Token.h"

#include <sstream>

#include "expr/OperatorTable.h"

namespace expr {

std::string toString(const Token& token) {
    switch (token.type) {
        case TokenType::Number: {
            std::ostringstream out;
            out << token.number;
            return out.str();
        }
        case TokenType::Operator:
            if (token.op == OperatorTable::kUnaryMinus) return "u-";
            if (token.op == OperatorTable::kUnaryPlus) return "u+";
            return std::string(1, token.op);
        case TokenType::LeftParen:
            return "(";
        case TokenType::RightParen:
            return ")";
    }
    return "?";
}

}  // namespace expr
