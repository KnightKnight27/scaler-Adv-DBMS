#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal_manager.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "txn/lock_manager.h"
#include "txn/transaction_manager.h"

using namespace minidb;

namespace {

std::vector<std::string> ReferencedTables(const Statement &stmt) {
  switch (stmt.kind) {
    case StmtKind::CREATE_TABLE: return {stmt.create_table.table_name};
    case StmtKind::INSERT: return {stmt.insert.table_name};
    case StmtKind::DELETE: return {stmt.del.table_name};
    case StmtKind::SELECT: {
      std::vector<std::string> tables{stmt.select.from_table};
      if (stmt.select.has_join) tables.push_back(stmt.select.join_table);
      return tables;
    }
  }
  return {};
}

bool IsWrite(const Statement &stmt) {
  return stmt.kind == StmtKind::INSERT || stmt.kind == StmtKind::DELETE || stmt.kind == StmtKind::CREATE_TABLE;
}

void PrintResult(const QueryResult &r, bool explain) {
  if (!r.ok) {
    std::cout << "ERROR: " << r.message << "\n";
    return;
  }
  if (explain && !r.explain.empty()) std::cout << "--- query plan ---\n" << r.explain;
  if (!r.column_names.empty() || !r.rows.empty()) {
    for (size_t i = 0; i < r.column_names.size(); i++)
      std::cout << r.column_names[i] << (i + 1 < r.column_names.size() ? " | " : "\n");
    for (auto &row : r.rows) {
      for (size_t i = 0; i < row.size(); i++) std::cout << row[i] << (i + 1 < row.size() ? " | " : "\n");
    }
  }
  std::cout << r.message << "\n";
}

}  // namespace

int main(int argc, char **argv) {
  std::string db_file = argc > 1 ? argv[1] : "minidb.db";

  DiskManager dm(db_file);
  BufferPool bpm(128, &dm);
  Catalog catalog(&bpm, db_file + ".meta");
  WalManager wal(&dm);
  LockManager lock_mgr;
  TransactionManager txn_mgr(&lock_mgr);
  Executor executor(&catalog);

  {
    RecoveryManager recovery(&dm, &bpm, &catalog);
    int redone = recovery.Recover();
    if (redone > 0) std::cout << "Recovery: redid " << redone << " logged operation(s) from a prior crash.\n";
  }

  std::cout << "MiniDB - db file: " << db_file << "\n"
            << "Commands: BEGIN / COMMIT / ABORT, EXPLAIN <select>, CRASH (kill -9 self for demo), quit\n";

  std::unique_ptr<Transaction> active_txn;
  bool explain_mode = false;

  std::string line;
  while (true) {
    std::cout << (active_txn ? "minidb*> " : "minidb> ");
    if (!std::getline(std::cin, line)) break;
    if (line.empty()) continue;
    if (line == "quit" || line == "exit") break;

    if (line == "BEGIN" || line == "begin") {
      if (active_txn) { std::cout << "ERROR: already inside a transaction\n"; continue; }
      active_txn = txn_mgr.Begin();
      wal.AppendBegin(active_txn->Id());
      std::cout << "Started transaction " << active_txn->Id() << "\n";
      continue;
    }
    if (line == "COMMIT" || line == "commit") {
      if (!active_txn) { std::cout << "ERROR: no active transaction\n"; continue; }
      wal.AppendCommit(active_txn->Id());
      txn_mgr.Commit(active_txn.get());
      std::cout << "Transaction " << active_txn->Id() << " committed\n";
      active_txn.reset();
      continue;
    }
    if (line == "ABORT" || line == "abort" || line == "ROLLBACK" || line == "rollback") {
      if (!active_txn) { std::cout << "ERROR: no active transaction\n"; continue; }
      RecoveryManager recovery(&dm, &bpm, &catalog);
      recovery.UndoTransaction(active_txn->Id());
      wal.AppendAbort(active_txn->Id());
      txn_mgr.Abort(active_txn.get());
      std::cout << "Transaction " << active_txn->Id() << " aborted and rolled back\n";
      active_txn.reset();
      continue;
    }
    if (line == "CRASH") {
      std::cout << "Simulating an unclean crash (no flush) - restart minidb to recover.\n";
      std::_Exit(1);
    }

    bool show_explain = explain_mode;
    std::string sql = line;
    if (sql.rfind("EXPLAIN ", 0) == 0 || sql.rfind("explain ", 0) == 0) {
      show_explain = true;
      sql = sql.substr(8);
    }

    try {
      Lexer lex(sql);
      Parser parser(lex.Tokenize());
      Statement stmt = parser.ParseStatement();

      // Implicit autocommit transaction when there's no explicit BEGIN, so
      // every statement still goes through 2PL + WAL logging.
      Transaction *txn = active_txn.get();
      std::unique_ptr<Transaction> implicit;
      if (!txn) {
        implicit = txn_mgr.Begin();
        wal.AppendBegin(implicit->Id());
        txn = implicit.get();
      }

      try {
        for (auto &table : ReferencedTables(stmt)) {
          if (IsWrite(stmt)) lock_mgr.LockExclusive(txn, table);
          else lock_mgr.LockShared(txn, table);
        }
      } catch (const TransactionAbortedException &e) {
        std::cout << "ERROR: " << e.what() << "\n";
        if (implicit) txn_mgr.Abort(implicit.get());
        else { RecoveryManager r(&dm, &bpm, &catalog); r.UndoTransaction(txn->Id()); txn_mgr.Abort(txn); active_txn.reset(); }
        continue;
      }

      QueryResult r = executor.Execute(stmt, txn->Id(), &wal);
      PrintResult(r, show_explain);

      if (implicit) {
        wal.AppendCommit(implicit->Id());
        txn_mgr.Commit(implicit.get());
      }
    } catch (const std::exception &e) {
      std::cout << "ERROR: " << e.what() << "\n";
    }
  }

  if (active_txn) {
    std::cout << "Exiting with an open transaction - rolling it back.\n";
    RecoveryManager recovery(&dm, &bpm, &catalog);
    recovery.UndoTransaction(active_txn->Id());
    wal.AppendAbort(active_txn->Id());
    txn_mgr.Abort(active_txn.get());
  }
  return 0;
}
