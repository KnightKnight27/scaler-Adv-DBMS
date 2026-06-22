#pragma once
#include <string>
#include <vector>
#include "ast.hpp"
#include "../common/value.hpp"
#include "../catalog/database.hpp"

namespace minidb {

// The outcome of running one statement: either a query result set (columns +
// rows) or a status message (for DDL/DML/transaction control).
struct ExecResult {
    bool is_query = false;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    std::string message;
};

// Plans and runs a single parsed statement against a Database. Queries are
// executed with a tree of Volcano-style pull operators (see executor.cpp).
class Executor {
public:
    explicit Executor(Database& db) : db_(db) {}
    ExecResult execute(const Statement& st);

private:
    Database& db_;

    ExecResult runSelect(const SelectStmt& s);
    ExecResult runInsert(const InsertStmt& s);
    ExecResult runDelete(const DeleteStmt& s);
    ExecResult runCreate(const CreateStmt& s);
};

} // namespace minidb
