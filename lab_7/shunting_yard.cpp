//
// shunting_yard.cpp
// ---------------------------------------------------------------------------
// Implementation of Dijkstra's Shunting-Yard conversion and the RPN evaluator.
// ---------------------------------------------------------------------------

#include "shunting_yard.h"
#include <stack>
#include <stdexcept>
#include <string>

namespace {

// --- Operator classification ------------------------------------------------

// Is this token an operator we route through the shunting-yard?
bool isOperator(TokenType t) {
    switch (t) {
        case TokenType::OR:
        case TokenType::AND:
        case TokenType::NOT:
        case TokenType::EQ:  case TokenType::NEQ:
        case TokenType::GT:  case TokenType::LT:
        case TokenType::GTE: case TokenType::LTE:
        case TokenType::PLUS: case TokenType::MINUS:
        case TokenType::STAR: case TokenType::SLASH:
            return true;
        default:
            return false;
    }
}

// An operand is anything that gets pushed straight onto the output queue.
bool isOperand(TokenType t) {
    return t == TokenType::IDENTIFIER ||
           t == TokenType::NUMBER     ||
           t == TokenType::STRING;
}

// PRECEDENCE TABLE
// Higher number == binds tighter (popped/applied first).
//
//   NOT (unary)                : 6   (highest among logical, right-assoc)
//   * /                        : 5
//   + -                        : 4
//   > < >= <= = !=  (compare)  : 3
//   AND                        : 2
//   OR                         : 1   (lowest)
//
int precedence(TokenType t) {
    switch (t) {
        case TokenType::NOT:                       return 6;
        case TokenType::STAR: case TokenType::SLASH: return 5;
        case TokenType::PLUS: case TokenType::MINUS: return 4;
        case TokenType::EQ:  case TokenType::NEQ:
        case TokenType::GT:  case TokenType::LT:
        case TokenType::GTE: case TokenType::LTE:   return 3;
        case TokenType::AND:                        return 2;
        case TokenType::OR:                         return 1;
        default:                                    return 0;
    }
}

// Associativity: NOT is unary and right-associative; every binary operator
// here is left-associative.
bool isRightAssociative(TokenType t) {
    return t == TokenType::NOT;
}

} // namespace

// ---------------------------------------------------------------------------
// toRPN : Dijkstra's Shunting-Yard
//
//   for each token:
//     operand            -> push to output queue
//     '('                -> push to operator stack
//     ')'                -> pop operators to output until matching '('
//     operator o1:
//        while there is an operator o2 on top of the stack with
//            (o2 has higher precedence) OR
//            (equal precedence AND o1 is left-associative)
//          pop o2 to output
//        push o1
//   at end: pop any remaining operators to output
// ---------------------------------------------------------------------------
std::vector<Token> toRPN(const std::vector<Token> &tokens) {
    std::vector<Token> output;          // the output queue (built as a vector)
    std::stack<Token> ops;              // the operator stack

    for (const Token &tok : tokens) {
        if (tok.type == TokenType::END) {
            break;                      // trailing sentinel, ignore
        }

        if (isOperand(tok.type)) {
            // Operands flow directly to the output.
            output.push_back(tok);
        }
        else if (tok.type == TokenType::LPAREN) {
            ops.push(tok);
        }
        else if (tok.type == TokenType::RPAREN) {
            // Pop until the matching '('.
            while (!ops.empty() && ops.top().type != TokenType::LPAREN) {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) {
                throw std::runtime_error("Mismatched parentheses: missing '('");
            }
            ops.pop();                  // discard the '('
        }
        else if (isOperator(tok.type)) {
            // The classic precedence/associativity drain.
            while (!ops.empty() && ops.top().type != TokenType::LPAREN) {
                TokenType top = ops.top().type;
                bool topWins =
                    precedence(top) > precedence(tok.type) ||
                    (precedence(top) == precedence(tok.type) &&
                     !isRightAssociative(tok.type));
                if (!topWins) break;
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(tok);
        }
        else {
            throw std::runtime_error(
                std::string("Unexpected token in WHERE expression: ") +
                tokenTypeName(tok.type));
        }
    }

    // Drain remaining operators.
    while (!ops.empty()) {
        if (ops.top().type == TokenType::LPAREN) {
            throw std::runtime_error("Mismatched parentheses: extra '('");
        }
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}

// ---------------------------------------------------------------------------
// evalRPN : evaluate postfix against a Row using a value stack.
// ---------------------------------------------------------------------------
namespace {

// Resolve an operand token to a concrete Value for this row.
Value operandValue(const Token &tok, const Row &row) {
    switch (tok.type) {
        case TokenType::NUMBER:
            return Value(static_cast<long long>(std::stoll(tok.text)));
        case TokenType::STRING:
            return Value(tok.text);
        case TokenType::IDENTIFIER: {
            auto it = row.find(tok.text);
            if (it == row.end()) {
                throw std::runtime_error("Unknown column: " + tok.text);
            }
            return it->second;
        }
        default:
            throw std::runtime_error("Not an operand");
    }
}

long long toBool(const Value &v) {
    // Treat any value as a truthiness via its integer form (booleans are 0/1).
    return asInt(v) != 0 ? 1 : 0;
}

// Apply a comparison operator. Supports int<->int and string<->string.
Value applyComparison(TokenType op, const Value &l, const Value &r) {
    bool result;
    if (isStr(l) || isStr(r)) {
        // String comparison: only = and != are meaningful here.
        const std::string &a = asStr(l);
        const std::string &b = asStr(r);
        switch (op) {
            case TokenType::EQ:  result = (a == b); break;
            case TokenType::NEQ: result = (a != b); break;
            case TokenType::GT:  result = (a >  b); break;
            case TokenType::LT:  result = (a <  b); break;
            case TokenType::GTE: result = (a >= b); break;
            case TokenType::LTE: result = (a <= b); break;
            default: throw std::runtime_error("Bad comparison operator");
        }
    } else {
        long long a = asInt(l), b = asInt(r);
        switch (op) {
            case TokenType::EQ:  result = (a == b); break;
            case TokenType::NEQ: result = (a != b); break;
            case TokenType::GT:  result = (a >  b); break;
            case TokenType::LT:  result = (a <  b); break;
            case TokenType::GTE: result = (a >= b); break;
            case TokenType::LTE: result = (a <= b); break;
            default: throw std::runtime_error("Bad comparison operator");
        }
    }
    return Value(static_cast<long long>(result ? 1 : 0));
}

Value applyArithmetic(TokenType op, const Value &l, const Value &r) {
    long long a = asInt(l), b = asInt(r);
    switch (op) {
        case TokenType::PLUS:  return Value(a + b);
        case TokenType::MINUS: return Value(a - b);
        case TokenType::STAR:  return Value(a * b);
        case TokenType::SLASH:
            if (b == 0) throw std::runtime_error("Division by zero");
            return Value(a / b);
        default: throw std::runtime_error("Bad arithmetic operator");
    }
}

} // namespace

Value evalRPN(const std::vector<Token> &rpn, const Row &row) {
    std::stack<Value> st;

    auto pop = [&]() -> Value {
        if (st.empty()) throw std::runtime_error("Malformed expression (stack underflow)");
        Value v = st.top();
        st.pop();
        return v;
    };

    for (const Token &tok : rpn) {
        if (isOperand(tok.type)) {
            st.push(operandValue(tok, row));
            continue;
        }

        switch (tok.type) {
            case TokenType::NOT: {                      // unary
                Value a = pop();
                st.push(Value(static_cast<long long>(toBool(a) ? 0 : 1)));
                break;
            }
            case TokenType::AND: {
                Value r = pop(), l = pop();
                st.push(Value(static_cast<long long>((toBool(l) && toBool(r)) ? 1 : 0)));
                break;
            }
            case TokenType::OR: {
                Value r = pop(), l = pop();
                st.push(Value(static_cast<long long>((toBool(l) || toBool(r)) ? 1 : 0)));
                break;
            }
            case TokenType::EQ:  case TokenType::NEQ:
            case TokenType::GT:  case TokenType::LT:
            case TokenType::GTE: case TokenType::LTE: {
                Value r = pop(), l = pop();
                st.push(applyComparison(tok.type, l, r));
                break;
            }
            case TokenType::PLUS: case TokenType::MINUS:
            case TokenType::STAR: case TokenType::SLASH: {
                Value r = pop(), l = pop();
                st.push(applyArithmetic(tok.type, l, r));
                break;
            }
            default:
                throw std::runtime_error(
                    std::string("Unexpected token in RPN: ") + tokenTypeName(tok.type));
        }
    }

    if (st.size() != 1) {
        throw std::runtime_error("Malformed expression (leftover operands)");
    }
    return st.top();
}

bool evalPredicate(const std::vector<Token> &rpn, const Row &row) {
    return toBool(evalRPN(rpn, row)) != 0;
}
