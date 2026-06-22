#pragma once
#include "engine/database.h"
#include "sql/ast.h"
#include "optimizer/optimizer.h"

namespace minidb {

// Per-column metadata for an intermediate result row (carries the originating
// table so qualified column references like "users.id" resolve in joins).
struct ColMeta {
    std::string table;
    std::string name;
    TypeId      type;
};

// An intermediate relation: column layout + materialised rows. MiniDB uses a
// simple materialised (non-pipelined) model, which is easy to reason about.
struct RowSet {
    std::vector<ColMeta> cols;
    std::vector<Tuple>   rows;
    std::vector<RID>     rids; // parallel to rows for single-table scans (DELETE)
};

// The Executor turns an AST + optimizer plan into results, acquiring the right
// locks (strict 2PL) and writing WAL records via the Database for mutations.
class Executor {
public:
    Executor(Database &db, Transaction *txn) : db_(db), txn_(txn) {}

    QueryResult run_select(const SelectStmt &stmt);
    QueryResult run_insert(const InsertStmt &stmt);
    QueryResult run_delete(const DeleteStmt &stmt);

private:
    // Read one table using its access plan; applies residual filters.
    RowSet scan_table(const TablePlan &plan);

    // Project `select list` (or *) out of an intermediate RowSet.
    QueryResult project(const SelectStmt &stmt, const RowSet &rs);

    Database     &db_;
    Transaction  *txn_;
};

} // namespace minidb
