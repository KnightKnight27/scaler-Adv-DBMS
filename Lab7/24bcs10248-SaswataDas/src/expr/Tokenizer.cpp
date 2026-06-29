#include "expr/Tokenizer.h"

#include <cctype>
#include <stdexcept>
#include <string>

#include "expr/OperatorTable.h"

namespace expr {

namespace {

bool isBinaryOperatorChar(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^';
}

// A '+' or '-' is unary (prefix) when it appears at the start of the
// expression, right after another operator, or right after a '('.
bool prefixPositionExpectsUnary(const std::vector<Token>& produced) {
    if (produced.empty()) return true;
    const Token& prev = produced.back();
    return prev.type == TokenType::Operator || prev.type == TokenType::LeftParen;
}

}  // namespace

std::vector<Token> Tokenizer::tokenize(const std::string& input) const {
    std::vector<Token> tokens;
    size_t i = 0;
    const size_t n = input.size();

    while (i < n) {
        char c = input[i];

        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t start = i;
            bool seenDot = false;
            while (i < n &&
                   (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.')) {
                if (input[i] == '.') {
                    if (seenDot) throw std::runtime_error("malformed number: multiple decimal points");
                    seenDot = true;
                }
                ++i;
            }
            tokens.push_back(Token::makeNumber(std::stod(input.substr(start, i - start))));
            continue;
        }

        if (c == '(') {
            tokens.push_back(Token::makeLeftParen());
            ++i;
            continue;
        }

        if (c == ')') {
            tokens.push_back(Token::makeRightParen());
            ++i;
            continue;
        }

        if (isBinaryOperatorChar(c)) {
            if ((c == '-' || c == '+') && prefixPositionExpectsUnary(tokens)) {
                tokens.push_back(Token::makeOperator(
                    c == '-' ? OperatorTable::kUnaryMinus : OperatorTable::kUnaryPlus));
            } else {
                tokens.push_back(Token::makeOperator(c));
            }
            ++i;
            continue;
        }

        throw std::runtime_error(std::string("unexpected character in expression: ") + c);
    }

    return tokens;
}

}  // namespace expr
