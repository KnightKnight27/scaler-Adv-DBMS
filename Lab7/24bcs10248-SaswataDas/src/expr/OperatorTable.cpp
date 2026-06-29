#include "expr/OperatorTable.h"

#include <stdexcept>
#include <string>

namespace expr {

OperatorTable::OperatorTable() {
    // Binary operators.
    add('+', 2, Associativity::Left, false);
    add('-', 2, Associativity::Left, false);
    add('*', 3, Associativity::Left, false);
    add('/', 3, Associativity::Left, false);
    add('%', 3, Associativity::Left, false);
    add('^', 4, Associativity::Right, false);

    // Prefix (unary) operators bind tightest so that, for example,
    // -2^2 parses as (-2)^2. This choice is documented in the README.
    add(kUnaryMinus, 5, Associativity::Right, true);
    add(kUnaryPlus, 5, Associativity::Right, true);
}

void OperatorTable::add(char symbol, int precedence, Associativity associativity, bool unary) {
    operators_[symbol] = OperatorInfo{precedence, associativity, unary};
}

bool OperatorTable::isOperator(char symbol) const {
    return operators_.find(symbol) != operators_.end();
}

const OperatorInfo& OperatorTable::info(char symbol) const {
    auto it = operators_.find(symbol);
    if (it == operators_.end()) {
        throw std::runtime_error(std::string("unknown operator: ") + symbol);
    }
    return it->second;
}

}  // namespace expr
