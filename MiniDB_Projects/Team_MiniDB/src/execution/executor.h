#pragma once

#include <string>

#include "catalog/catalog.h"
#include "engine/storage_engine.h"
#include "parser/ast.h"

namespace minidb {

// The Executor drives a SQL statement end to end: it parses, plans (for SELECT,
// via the Optimizer), runs the operator pipeline or the storage engine, and
// prints results / feedback. It is the seam where M4 will add lock acquisition.
class Executor {
public:
    Executor(Catalog* cat, StorageEngine* engine) : cat_(cat), engine_(engine) {}

    // Run one or more ';'-separated statements.
    void execute_script(const std::string& sql);

private:
    void exec_statement(Statement& stmt);
    void exec_create(CreateTableStmt& s);
    void exec_insert(InsertStmt& s);
    void exec_delete(DeleteStmt& s);
    void exec_select(SelectStmt& s);

    Catalog*       cat_;
    StorageEngine* engine_;
};

} // namespace minidb
