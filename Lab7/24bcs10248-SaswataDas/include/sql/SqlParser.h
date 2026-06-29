#pragma once

#include <string>
#include <vector>

#include "Ast.h"
#include "SqlToken.h"

namespace sql {

// Recursive-descent parser that builds a SelectStatement (and a WHERE
// predicate tree) from a token stream.
//
// Grammar:
//   select     := SELECT projection FROM identifier [ WHERE orExpr ] END
//   projection := '*' | identifier ( ',' identifier )*
//   orExpr     := andExpr ( OR andExpr )*
//   andExpr    := primary ( AND primary )*
//   primary    := '(' orExpr ')' | comparison
//   comparison := identifier compOp operand
//   operand    := number | string
class SqlParser {
public:
    explicit SqlParser(std::vector<SqlToken> tokens);

    SelectStatement parse();

private:
    std::vector<SqlToken> tokens_;
    size_t pos_ = 0;

    const SqlToken& peek() const;
    const SqlToken& advance();
    bool check(SqlTokenType type) const;
    bool match(SqlTokenType type);
    const SqlToken& expect(SqlTokenType type, const std::string& what);

    void parseProjection(SelectStatement& stmt);
    BoolExprPtr parseOr();
    BoolExprPtr parseAnd();
    BoolExprPtr parsePrimary();
    BoolExprPtr parseComparison();
};

}  // namespace sql
