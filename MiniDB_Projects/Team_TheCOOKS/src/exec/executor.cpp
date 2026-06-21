#include "exec/executor.h"

#include <stdexcept>

#include "catalog/table.h"
#include "catalog/tuple.h"
#include "exec/operators.h"
#include "exec/planner.h"
#include "recovery/wal_manager.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"

namespace walterdb {

namespace {
// Acquire a 2PL lock on a whole table for ctx's transaction.  Returns false if
// the transaction was chosen as a deadlock victim (caller must abort).  A null
// context / txn means "no transactional locking" and always succeeds.
bool lock_table(ExecContext* ctx, uint32_t table_id, LockMode mode) {
  if (!ctx || !ctx->txn || !ctx->lock_mgr) return true;
  uint64_t res = lock_id_for_table(table_id);
  return mode == LockMode::Shared ? ctx->lock_mgr->lock_shared(ctx->txn, res)
                                  : ctx->lock_mgr->lock_exclusive(ctx->txn, res);
}

ExecResult deadlock_result() {
  ExecResult r;
  r.ok = false;
  r.aborted = true;
  r.error = "deadlock detected: transaction aborted (chosen as victim)";
  return r;
}
}  // namespace

ExecResult Executor::execute(const Statement* stmt, ExecContext* ctx) {
  try {
    switch (stmt->kind) {
      case StmtKind::Select: return exec_select(static_cast<const SelectStmt&>(*stmt), ctx);
      case StmtKind::Insert: return exec_insert(static_cast<const InsertStmt&>(*stmt), ctx);
      case StmtKind::Delete: return exec_delete(static_cast<const DeleteStmt&>(*stmt), ctx);
      case StmtKind::CreateTable: return exec_create(static_cast<const CreateTableStmt&>(*stmt));
      case StmtKind::Explain: return exec_explain(static_cast<const ExplainStmt&>(*stmt));
      case StmtKind::Begin: {
        ExecResult r; r.message = "BEGIN"; return r;
      }
      case StmtKind::Commit: {
        ExecResult r; r.message = "COMMIT"; return r;
      }
      case StmtKind::Abort: {
        ExecResult r; r.message = "ROLLBACK"; return r;
      }
    }
  } catch (const std::exception& e) {
    return ExecResult::fail(e.what());
  }
  return ExecResult::fail("unsupported statement");
}

ExecResult Executor::exec_select(const SelectStmt& s, ExecContext* ctx) {
  // Strict 2PL: take shared locks on every table the query reads.
  std::vector<const TableRef*> refs = {&s.from};
  for (const JoinClause& j : s.joins) refs.push_back(&j.right);
  for (const TableRef* ref : refs) {
    Table* t = catalog_.open_table(ref->table);
    if (t && !lock_table(ctx, t->info()->table_id, LockMode::Shared)) return deadlock_result();
  }

  Planner planner(catalog_);
  PlannedQuery pq = planner.plan_select(s);

  ExecResult r;
  r.is_select = true;
  r.columns = pq.column_names;
  pq.root->open();
  while (auto row = pq.root->next()) r.rows.push_back(std::move(*row));
  pq.root->close();
  return r;
}

ExecResult Executor::exec_explain(const ExplainStmt& s) {
  if (!s.inner || s.inner->kind != StmtKind::Select) {
    return ExecResult::fail("EXPLAIN currently supports SELECT only");
  }
  Planner planner(catalog_);
  PlannedQuery pq = planner.plan_select(static_cast<const SelectStmt&>(*s.inner));
  ExecResult r;
  r.message = pq.explain;
  return r;
}

ExecResult Executor::exec_create(const CreateTableStmt& s) {
  Schema schema(s.columns);
  Status st = catalog_.create_table(s.table, schema);
  if (!st.ok()) {
    if (s.if_not_exists && st.code() == StatusCode::AlreadyExists) {
      ExecResult r; r.message = "Table '" + s.table + "' already exists (skipped)"; return r;
    }
    return ExecResult::fail(st.message());
  }
  ExecResult r;
  r.message = "Table '" + s.table + "' created";
  return r;
}

ExecResult Executor::exec_insert(const InsertStmt& s, ExecContext* ctx) {
  Table* t = catalog_.open_table(s.table);
  if (!t) return ExecResult::fail("no such table '" + s.table + "'");
  if (!lock_table(ctx, t->info()->table_id, LockMode::Exclusive)) return deadlock_result();
  const Schema& sch = t->schema();

  // Map each provided value position to a schema column index.
  std::vector<size_t> target;
  if (s.columns.empty()) {
    for (size_t i = 0; i < sch.num_columns(); ++i) target.push_back(i);
  } else {
    for (const std::string& name : s.columns) {
      auto idx = sch.index_of(name);
      if (!idx) return ExecResult::fail("unknown column '" + name + "'");
      target.push_back(*idx);
    }
  }

  ResultSchema empty_schema;
  Row empty_row;
  uint64_t count = 0;
  for (const auto& exprs : s.rows) {
    if (exprs.size() != target.size()) {
      return ExecResult::fail("INSERT has " + std::to_string(exprs.size()) +
                              " values but expected " + std::to_string(target.size()));
    }
    // Start with a fully-NULL tuple, then fill the provided columns.
    std::vector<Value> vals;
    vals.reserve(sch.num_columns());
    for (size_t i = 0; i < sch.num_columns(); ++i) vals.push_back(Value::make_null(sch.column(i).type));

    for (size_t k = 0; k < exprs.size(); ++k) {
      Value v = evaluate(exprs[k].get(), empty_row, empty_schema);  // constants only
      auto cv = coerce(v, sch.column(target[k]).type);
      if (!cv) {
        return ExecResult::fail("type mismatch for column '" + sch.column(target[k]).name + "'");
      }
      vals[target[k]] = std::move(*cv);
    }

    Tuple tuple(std::move(vals));
    // Pre-check PK uniqueness so we only log operations that will succeed
    // (write-ahead: the WAL record precedes the data change).
    if (t->info()->pk_column >= 0) {
      const Value& pk = tuple.value(static_cast<size_t>(t->info()->pk_column));
      if (pk.is_null()) return ExecResult::fail("primary key cannot be NULL");
      if (t->lookup_pk(pk).has_value())
        return ExecResult::fail("duplicate primary key " + pk.to_string());
    }
    std::string row_bytes = tuple.encode(sch);
    uint32_t tid = t->info()->table_id;
    if (ctx && ctx->wal && ctx->txn) ctx->wal->log_insert(ctx->txn->id(), tid, row_bytes);

    Status st = t->insert(tuple);
    if (!st.ok()) return ExecResult::fail(st.message());
    if (ctx && ctx->txn) ctx->txn->record_insert(tid, std::move(row_bytes));
    ++count;
  }

  ExecResult r;
  r.affected = count;
  r.message = std::to_string(count) + " row(s) inserted";
  return r;
}

ExecResult Executor::exec_delete(const DeleteStmt& s, ExecContext* ctx) {
  Table* t = catalog_.open_table(s.table);
  if (!t) return ExecResult::fail("no such table '" + s.table + "'");
  if (!lock_table(ctx, t->info()->table_id, LockMode::Exclusive)) return deadlock_result();

  ResultSchema rs = ResultSchema::from_table(t->schema(), t->info()->name);

  // Collect matching RIDs first, then delete -- never mutate during the scan.
  std::vector<RID> to_delete;
  for (auto c = t->heap()->scan(); c.valid(); c.next()) {
    if (s.where) {
      Tuple tup = Tuple::decode(t->schema(), c.value());
      if (!evaluate_predicate(s.where.get(), tup.values(), rs)) continue;
    }
    to_delete.push_back(c.rid());
  }

  uint32_t tid = t->info()->table_id;
  for (RID rid : to_delete) {
    auto row = t->get(rid);
    if (!row) continue;
    std::string row_bytes = row->encode(t->schema());  // image needed to undo
    if (ctx && ctx->wal && ctx->txn) ctx->wal->log_delete(ctx->txn->id(), tid, row_bytes);
    t->erase(rid);
    if (ctx && ctx->txn) ctx->txn->record_delete(tid, std::move(row_bytes));
  }

  ExecResult r;
  r.affected = to_delete.size();
  r.message = std::to_string(to_delete.size()) + " row(s) deleted";
  return r;
}

}  // namespace walterdb
