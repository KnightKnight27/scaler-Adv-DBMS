// Recursive-descent SQL parser producing the AST in ast.hpp.
//
// Grammar (informal):
//   stmt   := create | insert | select | delete | update | txn
//   create := CREATE TABLE id '(' coldef (',' coldef)* ')'
//   insert := INSERT INTO id ['(' id (',' id)* ')'] VALUES tuple (',' tuple)*
//   select := SELECT items FROM tableref (JOIN tableref ON expr)*
//             [WHERE expr] [GROUP BY cols] [ORDER BY col [ASC|DESC]]
//   delete := DELETE FROM id [WHERE expr]
//   update := UPDATE id SET assign (',' assign)* [WHERE expr]
//   txn    := BEGIN | COMMIT | ABORT | ROLLBACK
//   expr   := or_expr ; or_expr := and_expr (OR and_expr)* ; etc.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/ast.hpp"
#include "sql/lexer.hpp"

namespace minidb {

class Parser {
public:
    explicit Parser(const std::string& sql);
    std::unique_ptr<Statement> parse();   // parse a single statement

private:
    // statement parsers
    std::unique_ptr<Statement> parse_create();
    std::unique_ptr<Statement> parse_insert();
    std::unique_ptr<Statement> parse_select();
    std::unique_ptr<Statement> parse_delete();
    std::unique_ptr<Statement> parse_update();

    // expression parsers (precedence climbing)
    ExprPtr parse_expr();        // OR level
    ExprPtr parse_and();         // AND level
    ExprPtr parse_comparison();  // = < > etc.
    ExprPtr parse_primary();     // column / literal / ( expr )

    SelectItem parse_select_item();
    TableRef   parse_table_ref();
    Value      parse_value();

    // token helpers
    const Token& cur() const { return toks_[pos_]; }
    const Token& peek(int n = 1) const;
    bool at_end() const { return cur().kind == TokKind::END; }
    Token advance() { return toks_[pos_++]; }
    bool check(TokKind k) const { return cur().kind == k; }
    bool match(TokKind k);
    Token expect(TokKind k, const char* what);

    bool is_kw(const std::string& kw) const;    // current token == keyword?
    bool match_kw(const std::string& kw);        // consume if keyword matches
    void expect_kw(const std::string& kw);
    static std::string upper(std::string s);

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

}  // namespace minidb
