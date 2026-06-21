// SQL front end: turn a query string into a small abstract syntax tree.
//
//   tokenize()  breaks text into tokens (keywords, names, numbers, strings, ops)
//   Parser      a recursive-descent parser that builds one of four statements
//
// Supported grammar (small on purpose):
//   CREATE TABLE name (col INT|TEXT, ...)
//   INSERT INTO name VALUES (v, ...)
//   SELECT  * | col[, col]...  FROM t [JOIN t2 ON t.a = t2.b] [WHERE cond [AND cond]...]
//   DELETE FROM name [WHERE cond [AND cond]...]
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace minidb {

// ----- tokens -----
enum class Tok { Keyword, Name, Int, Str, Op, Sym, End };
struct Token {
    Tok kind;
    std::string text;
};
std::vector<Token> tokenize(const std::string& sql);

// ----- AST -----
struct ColRef {
    std::string table;  // empty if the column was written unqualified
    std::string name;
};

struct Condition {
    ColRef left;
    std::string op;
    bool hasLiteral = false;  // comparison against a constant (WHERE)
    Value literal;
    bool hasRight = false;    // comparison against another column (JOIN ON)
    ColRef right;
};

enum class StmtKind { Create, Insert, Select, Delete };

struct Statement {
    StmtKind kind;
    virtual ~Statement() = default;
};

struct CreateStmt : Statement {
    std::string table;
    std::vector<Column> columns;
    CreateStmt() { kind = StmtKind::Create; }
};

struct InsertStmt : Statement {
    std::string table;
    Row values;
    InsertStmt() { kind = StmtKind::Insert; }
};

struct SelectStmt : Statement {
    std::vector<ColRef> columns;  // empty means SELECT *
    std::string table;
    bool hasJoin = false;
    std::string joinTable;
    Condition joinCond;
    std::vector<Condition> where;  // AND-ed
    SelectStmt() { kind = StmtKind::Select; }
};

struct DeleteStmt : Statement {
    std::string table;
    std::vector<Condition> where;
    DeleteStmt() { kind = StmtKind::Delete; }
};

std::unique_ptr<Statement> parse(const std::string& sql);

}  // namespace minidb
