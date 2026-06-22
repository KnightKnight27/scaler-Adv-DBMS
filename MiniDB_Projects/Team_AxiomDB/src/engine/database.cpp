#include "engine/database.h"

#include "parser/parser.h"
#include "recovery/crash_recovery_manager.h"

namespace axiomdb {

Database::Database(const std::string& base_path, size_t buffer_frames)
    : disk_(base_path + ".wdb"),
      pool_(&disk_, buffer_frames, /*k=*/2),
      wal_(base_path + ".wal"),
      catalog_(&pool_, base_path + ".catalog"),
      txn_mgr_(&lock_mgr_, &wal_, &catalog_),
      exec_(catalog_) {
  // Write-ahead enforcement: the log must be durable before any data page is
  // written back to disk.
  pool_.set_pre_flush_hook([this] { wal_.sync(); });
  recover();
}

Database::~Database() {
  if (!crashed_) checkpoint();
}

void Database::recover() {
  auto records = wal_.read_all();
  if (records.empty()) return;
  CrashRecoveryManager rm(&catalog_);
  rm.run(records);
  // Persist the recovered state and start a fresh log.
  checkpoint();
}

void Database::checkpoint() {
  pool_.flush_all();
  disk_.sync();
  wal_.truncate();  // data file is now authoritative; the log is no longer needed
}

void Database::simulate_crash() { crashed_ = true; }

ExecResult Database::run(const std::string& sql) {
  ParseResult parsed = parse_sql(sql);
  if (!parsed.ok()) return ExecResult::fail(parsed.error);
  const Statement* stmt = parsed.statement.get();

  // Explicit transaction control.
  switch (stmt->kind) {
    case StmtKind::Begin: {
      if (current_txn_) return ExecResult::fail("already in a transaction");
      current_txn_ = txn_mgr_.begin();
      ExecResult r; r.message = "BEGIN"; return r;
    }
    case StmtKind::Commit: {
      if (!current_txn_) return ExecResult::fail("no transaction in progress");
      txn_mgr_.commit(current_txn_.get());
      current_txn_.reset();
      ExecResult r; r.message = "COMMIT"; return r;
    }
    case StmtKind::Abort: {
      if (!current_txn_) return ExecResult::fail("no transaction in progress");
      txn_mgr_.abort(current_txn_.get());
      current_txn_.reset();
      ExecResult r; r.message = "ROLLBACK"; return r;
    }
    default:
      break;
  }

  // DML/DDL/SELECT: run inside the active transaction, or an implicit
  // auto-commit one.
  bool auto_commit = !current_txn_;
  std::unique_ptr<Transaction> owned;
  Transaction* txn = current_txn_.get();
  if (auto_commit) {
    owned = txn_mgr_.begin();
    txn = owned.get();
  }

  ExecContext ctx{txn, &lock_mgr_, &wal_};
  ExecResult result = exec_.execute(stmt, &ctx);

  if (auto_commit) {
    if (result.ok) txn_mgr_.commit(txn);
    else txn_mgr_.abort(txn);
  } else if (!result.ok) {
    // A failed statement aborts the whole explicit transaction.
    txn_mgr_.abort(current_txn_.get());
    current_txn_.reset();
  }
  return result;
}

}  // namespace axiomdb
