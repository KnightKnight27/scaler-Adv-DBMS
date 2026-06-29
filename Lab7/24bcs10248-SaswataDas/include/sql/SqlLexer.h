#pragma once

#include <string>
#include <vector>

#include "SqlToken.h"

namespace sql {

// Turns a SELECT statement string into a token stream. Keywords are
// matched case-insensitively; string literals use single quotes.
class SqlLexer {
public:
    std::vector<SqlToken> tokenize(const std::string& input) const;
};

}  // namespace sql
