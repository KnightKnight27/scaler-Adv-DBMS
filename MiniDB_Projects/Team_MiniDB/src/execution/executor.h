#pragma once

#include <string>

#include "catalog/catalog.h"
#include "common/types.h"
#include "engine/storage_engine.h"
#include "parser/ast.h"
#include "recovery/log_manager.h"

namespace minidb {

// The Executor drives a SQL statement end to end: it parses, plans (for SELECT,
// via the Optimizer), runs the operator pipeline or the storage engine, and
// prints results / feedback.
//
// When given a LogManager it provides durability: each DML statement is one
// transaction whose PUT/ERASE ops are written to the WAL and the COMMIT is
// flushed before the statement returns (force-log-at-commit, NO-FORCE on data).
// DDL is checkpointed immediately so on-disk structures stay consistent.
class Executor {
public:
    Executor(Catalog* cat, StorageEngine* engine, LogManager* log = nullptr)
        : cat_(cat), engine_(engine), log_(log) {}

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
    LogManager*    log_;
    TxId           next_tx_ = 1;
};

} // namespace minidb
