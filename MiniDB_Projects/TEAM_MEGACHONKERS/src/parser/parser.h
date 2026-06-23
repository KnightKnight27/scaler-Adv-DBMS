#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include "parser/token.h"
#include "parser/ast.h"
#include "parser/expression.h"

namespace minidb {

// Thrown internally on a syntax error; the public Parse() entry point catches
// it and returns an InvalidStatement carrying the message, so callers never
// have to handle exceptions.
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

// Recursive-descent SQL parser. Consumes the token stream from the Lexer and
// produces a typed Statement AST. Grammar (precedence low -> high for the WHERE
// expression):
//
//   statement   := create | insert | select | delete | create_index
//                | BEGIN | COMMIT | ROLLBACK | EXIT
//   select      := SELECT (STAR | col_ref (',' col_ref)*) FROM ident
//                  (JOIN ident ON col_ref '=' col_ref)? (WHERE expr)?
//   delete      := DELETE FROM ident (WHERE expr)?
//   expr        := or_expr
//   or_expr     := and_expr (OR and_expr)*
//   and_expr    := predicate (AND predicate)*
//   predicate   := '(' expr ')' | operand compare_op operand
//   operand     := literal | col_ref
//   col_ref     := ident ('.' ident)?
class Parser {
public:
    explicit Parser(std::string sql);

    // Parses a single statement. Never throws: syntax errors come back as an
    // InvalidStatement whose `error` field explains the problem.
    StmtPtr Parse();

private:
    std::vector<Token> tokens_;
    size_t pos_{0};

    // --- token cursor helpers ---
    const Token& Peek() const;
    const Token& Previous() const;
    bool AtEnd() const;
    bool Check(TokenType type) const;
    bool Match(TokenType type);
    const Token& Advance();
    const Token& Expect(TokenType type, const std::string& what);

    // --- statement productions ---
    StmtPtr ParseStatement();
    StmtPtr ParseCreate();
    StmtPtr ParseInsert();
    StmtPtr ParseSelect();
    StmtPtr ParseDelete();

    // --- shared sub-productions ---
    SelectColumn ParseColumnRef();
    ColumnDefinition ParseColumnDefinition();
    std::vector<std::string> ParseValueTuple();

    // --- expression productions (WHERE / ON predicates) ---
    ExprPtr ParseExpression();
    ExprPtr ParseOr();
    ExprPtr ParseAnd();
    ExprPtr ParsePredicate();
    ExprPtr ParseOperand();
};

} // namespace minidb
