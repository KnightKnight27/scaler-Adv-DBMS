#pragma once

#include "Interfaces.h"

namespace expr {

// Splits an arithmetic expression string into tokens and resolves whether
// a leading '+'/'-' is a binary or a prefix (unary) operator, emitting the
// internal unary symbols when appropriate. Sole responsibility: lexing.
class Tokenizer : public ITokenizer {
public:
    std::vector<Token> tokenize(const std::string& input) const override;
};

}  // namespace expr
