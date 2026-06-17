// sql_engine.h — ADBMS Lab 7, 24BCS10115 Gauri Shukla
//
// A minimal in-memory SQL SELECT engine built on top of Dijkstra's
// shunting-yard algorithm. The pipeline mirrors a real database front end:
//
//     SQL text  --tokenize-->  tokens
//               --parse_select->  SelectStmt  (WHERE kept as RPN)
//               --execute------>  result Table   (filter, project, order, limit)
//
// The WHERE clause is compiled once, infix -> postfix (RPN), via shunting_yard()
// and then evaluated per row with a single stack in eval_rpn().
//
// Columns are typed: each cell is either a 64-bit integer or a text string
// (std::variant). Supported WHERE operators: = != < <= > >= AND OR NOT and
// parentheses.

#ifndef ADBMS_LAB7_SQL_ENGINE_H
#define ADBMS_LAB7_SQL_ENGINE_H

#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

namespace sqlmini {

// ---- data model -----------------------------------------------------------

using Value = std::variant<long long, std::string>;   // index 0 = int, 1 = text

struct Row {
    std::vector<Value> cells;          // positional, aligned with Table::columns
};

struct Table {
    std::string              name;
    std::vector<std::string> columns;  // declares column order
    std::vector<Row>         rows;

    int col_index(const std::string& c) const;   // -1 if the column is absent
};

// ---- tokenizer ------------------------------------------------------------

enum class Tok {
    Ident, Int, Str,                    // operands
    Op,                                 // comparison / AND / OR / NOT (see text)
    LParen, RParen, Comma, Star,
    Select, From, Where, Order, By, Asc, Desc, Limit,
    End
};

struct Token {
    Tok         kind;
    std::string text;          // identifier name, operator symbol, or raw string
    long long   ival = 0;      // valid when kind == Tok::Int
};

std::vector<Token> tokenize(const std::string& sql);

// ---- shunting-yard --------------------------------------------------------

// Convert an infix slice of WHERE tokens into postfix (RPN) order.
std::vector<Token> shunting_yard(const std::vector<Token>& infix);

// Evaluate a compiled RPN predicate against one row; returns its truth value.
bool eval_rpn(const std::vector<Token>& rpn, const Table& schema, const Row& r);

// Render an RPN token list back to a flat string (for tracing / the demo).
std::string rpn_to_string(const std::vector<Token>& rpn);

// ---- SELECT ---------------------------------------------------------------

struct SelectStmt {
    std::vector<std::string> projection;     // empty => SELECT *
    std::string              table;
    std::vector<Token>       where_rpn;      // empty => no filter
    std::string              order_by;       // "" => no ORDER BY
    bool                     order_desc = false;
    long long                limit = -1;     // -1 => no LIMIT
};

SelectStmt parse_select(const std::string& sql);

// Run the statement against a source table, returning the result set.
Table execute(const SelectStmt& q, const Table& src);

void print_table(const Table& t, std::ostream& os);

}  // namespace sqlmini

#endif  // ADBMS_LAB7_SQL_ENGINE_H
