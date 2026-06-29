#pragma once

#include "Interfaces.h"
#include "OperatorTable.h"

namespace expr {

// Dijkstra's Shunting-Yard algorithm: converts an infix token stream into
// Reverse Polish Notation. Precedence and associativity decisions are
// delegated entirely to the OperatorTable.
class ShuntingYard : public IInfixToPostfix {
public:
    ShuntingYard() = default;
    explicit ShuntingYard(OperatorTable table);

    std::vector<Token> toPostfix(const std::vector<Token>& infix) const override;

private:
    OperatorTable operators_;
};

}  // namespace expr
