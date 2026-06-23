// Lab 5 (Part 2) - Minimal SQL SELECT parser + executor (header)
//
// Parses a small subset of SELECT:
//
//   SELECT <cols | *> FROM <table>
//   [WHERE <expr>] [ORDER BY <col> [ASC|DESC]] [LIMIT <n>]
//
// and runs it over an in-memory vector<Row> (this stands in for what a
// real storage layer would hand back after fetching pages from disk).
// The WHERE clause is evaluated with the Lab 5 Part 1 shunting-yard code.

#ifndef LAB5_SQL_H_
#define LAB5_SQL_H_

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lab5 {

// A cell is either a number or a string.
using Value = std::variant<double, std::string>;

// A row is a set of named columns.
struct Row {
    std::unordered_map<std::string, Value> cols;
};

// Parsed form of a SELECT query.
struct SelectQuery {
    std::vector<std::string> columns;        // empty => SELECT *
    std::string              table;          // FROM <table>
    std::string              where_clause;   // raw text, "" if none
    std::string              order_by;       // "" if none
    bool                     order_asc = true;
    int                      limit = -1;      // -1 => no limit
};

// Parse a SELECT statement into a SelectQuery. Throws on obvious errors.
SelectQuery parse_select(const std::string& sql);

// Run a parsed query against pre-fetched data and return the result rows.
std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data);

// Pretty-print rows to stdout (used by the demo).
void print_rows(const std::vector<Row>& rows);

}  // namespace lab5

#endif  // LAB5_SQL_H_
