// sql_engine.h  —  ADBMS Lab 6  |  Patel Jash  |  24bcs10632
//
// Header defining a small in-memory SQL SELECT engine utilizing
// Dijkstra's Shunting-Yard algorithm for evaluating WHERE conditions.
//
// Pipeline:
//     Raw SQL  --scan_tokens()--> Token list
//              --parse_query()--> QueryStructure (WHERE stored as Postfix/RPN)
//              --run_query()----> Result Relation
//
// The WHERE expression is converted to Reverse Polish Notation (RPN) once.
// Subsequent row checks are done efficiently using a stack.
//
// Data cell variants: 64-bit signed int OR std::string.
// Allowed operations: =, !=, <, <=, >, >=, AND, OR, NOT, and parentheses.

#ifndef LAB6_PATEL_SQL_ENGINE_H
#define LAB6_PATEL_SQL_ENGINE_H

#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

namespace sql_processor {

// ---------------------------------------------------------------------------
// Base Data Structures
// ---------------------------------------------------------------------------

// A DataCell holds either a long long integer or a string.
using DataCell = std::variant<long long, std::string>;

// A Tuple represents a single row in our Relation.
struct Tuple {
    std::vector<DataCell> columns;
};

// Relation acts as an in-memory database table.
struct Relation {
    std::string              title;
    std::vector<std::string> col_names;   // The headers of the table
    std::vector<Tuple>       rows;

    // Returns the index of a column by name, or -1 if missing.
    int find_column_index(const std::string& col) const;
};

// ---------------------------------------------------------------------------
// Lexical Scanner / Tokenizer
// ---------------------------------------------------------------------------

enum class TokenType {
    Identifier, IntLiteral, StringLiteral,
    Operator,
    LeftParen, RightParen, Comma, Asterisk,
    KeySelect, KeyFrom, KeyWhere,
    KeyOrder, KeyBy, KeyAsc, KeyDesc, KeyLimit,
    EndOfFile
};

struct SQLToken {
    TokenType   type;
    std::string text;      // Original token text
    long long   number = 0; // Filled if type is IntLiteral
};

// Breaks raw SQL string into an array of tokens.
std::vector<SQLToken> scan_tokens(const std::string& sql_text);

// ---------------------------------------------------------------------------
// Shunting-Yard & Postfix Logic
// ---------------------------------------------------------------------------

// Converts an infix token sequence into postfix (RPN) notation.
std::vector<SQLToken> convert_to_postfix(const std::vector<SQLToken>& infix_seq);

// Evaluates a compiled postfix expression against a specific Tuple.
bool evaluate_postfix(const std::vector<SQLToken>& rpn_seq,
                      const Relation& schema, const Tuple& row);

// Flattens a postfix sequence into a string for debugging.
std::string postfix_to_string(const std::vector<SQLToken>& rpn_seq);

// ---------------------------------------------------------------------------
// Query Parser & Execution Engine
// ---------------------------------------------------------------------------

struct QueryStructure {
    std::vector<std::string> target_cols; // Columns to project. Empty means '*'
    std::string              source_table;
    std::vector<SQLToken>    where_rpn;   // Postfix predicate sequence
    std::string              order_col;   // Empty means no ORDER BY
    bool                     descending = false;
    long long                limit_rows = -1; // -1 means no LIMIT
};

// Parses a SELECT query string into an executable QueryStructure.
QueryStructure parse_query(const std::string& sql_text);

// Executes the parsed query against a source relation.
Relation run_query(const QueryStructure& query, const Relation& db_table);

// Displays the relation content in a structured grid format.
void render_relation(const Relation& rel, std::ostream& out);

}  // namespace sql_processor

#endif  // LAB6_PATEL_SQL_ENGINE_H
