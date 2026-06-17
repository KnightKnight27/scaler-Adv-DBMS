#pragma once
// parser.h — Hand-written SQL parser for the subset MiniDB supports.
//
// Supported statements:
//   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
//   INSERT INTO t VALUES (val, ...)
//   SELECT * | col,... FROM t [JOIN t2 ON t.col = t2.col] [WHERE cond [AND cond...]]
//   DELETE FROM t [WHERE cond [AND cond...]]
//   BEGIN | COMMIT | ABORT
//   EXPLAIN SELECT ...   (prefix: prints chosen plan instead of executing)
//
// All parsing is single-pass recursive descent with simple tokenization.
// The resulting Stmt is a flat struct so the engine can pattern-match on it
// without any visitor infrastructure.
#include "value.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace minidb {

// Comparison operators used in WHERE conditions.
enum class Op { EQ, NE, LT, LE, GT, GE };

// One WHERE predicate: col OP literal
struct Cond {
    std::string col;  // may be "table.col" for joins
    Op          op;
    Value       val;
};

// The kind of SQL statement.
enum class Kind { CREATE, INSERT, SELECT, DELETE, BEGIN, COMMIT, ABORT };

// One column definition (for CREATE TABLE).
struct ColDef {
    std::string name;
    Type        type;
    bool        pk = false;
};

// Flat representation of one parsed statement.
// Using a flat struct instead of a polymorphic AST keeps things simple and
// easy to inspect in the debugger or describe in a viva.
struct Stmt {
    Kind kind;
    bool explain = false;   // true when EXPLAIN precedes SELECT

    std::string table;

    // CREATE TABLE
    std::vector<ColDef> cols;

    // INSERT
    std::vector<Value> values;

    // SELECT
    bool                     star = false;     // SELECT *
    std::vector<std::string> sel_cols;         // SELECT col1, col2, ...
    bool                     has_join = false;
    std::string              join_table;
    std::string              join_left;        // t1.col from ON clause
    std::string              join_right;       // t2.col from ON clause

    // SELECT / DELETE
    std::vector<Cond> where;
};

struct ParseError : std::runtime_error {
    explicit ParseError(const std::string& m) : std::runtime_error(m) {}
};

Stmt parse(const std::string& sql);

} // namespace minidb
