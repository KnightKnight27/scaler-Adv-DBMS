#pragma once
//
// sql.h
// ---------------------------------------------------------------------------
// Minimal SQL SELECT parser + executor.
//
//   SELECT <col, col, ... | *> FROM <table> [WHERE <expr>]
//
// Parsing of SELECT/FROM/WHERE is a tiny recursive-descent driver (matching
// the repo's style), but the WHERE predicate itself is handed off verbatim to
// the shunting-yard engine. The executor resolves the table name against an
// in-code catalog, compiles the WHERE expression to RPN once, then evaluates
// it per row and projects the requested columns.
// ---------------------------------------------------------------------------

#include "lexer.h"
#include "value.h"
#include <string>
#include <vector>
#include <map>

// A parsed SELECT statement.
struct SelectStatement {
    bool selectAll = false;               // true when SELECT *
    std::vector<std::string> columns;     // projected columns (if not *)
    std::string tableName;
    bool hasWhere = false;
    std::vector<Token> whereInfix;        // raw WHERE tokens (infix), pre-shunting
};

// Catalog: a named set of in-memory tables.
using Catalog = std::map<std::string, Table>;

// Parse a token stream into a SelectStatement.
SelectStatement parseSelect(const std::vector<Token> &tokens);

// Execute a statement against the catalog, returning the projected result set.
Table executeSelect(const SelectStatement &stmt, const Catalog &catalog);

// One-shot: lex + parse + execute a SQL string.
Table runQuery(const std::string &sql, const Catalog &catalog);

// Pretty-print a result set to stdout.
void printResult(const Table &result);
