#include <cassert>
#include <cstdio>
#include <iostream>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal_manager.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

QueryResult Run(Executor &ex, const std::string &sql, txn_id_t txn = 0, WalManager *wal = nullptr) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  Statement stmt = parser.ParseStatement();
  return ex.Execute(stmt, txn, wal);
}

int main() {
  const char *db = "test_recovery.db";
  const char *meta = "test_recovery.meta";
  std::remove(db);
  std::remove((std::string(db) + ".wal").c_str());
  std::remove(meta);

  // --- Phase 1: commit two rows, then crash mid-transaction on a third. ---
  // BufferPool's destructor flushes all dirty pages, which would mask the
  // crash we're trying to simulate - so this phase deliberately leaks its
  // objects on the heap instead of letting them go out of scope, meaning
  // NOTHING reaches the data file. Only the WAL (flushed synchronously on
  // every append) is durable, exactly as it would be after a real crash.
  {
    auto *dm = new DiskManager(db);
    auto *bpm = new BufferPool(64, dm);
    auto *cat = new Catalog(bpm, meta);
    auto *ex = new Executor(cat);
    auto *wal = new WalManager(dm);

    Run(*ex, "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)");

    wal->AppendBegin(1);
    Run(*ex, "INSERT INTO users VALUES (1, 'alice')", 1, wal);
    Run(*ex, "INSERT INTO users VALUES (2, 'bob')", 1, wal);
    wal->AppendCommit(1);

    wal->AppendBegin(2);
    Run(*ex, "INSERT INTO users VALUES (3, 'carol')", 2, wal);
    // No commit for txn 2: this is the in-flight transaction that must be
    // undone. Catalog metadata (table/schema) is saved synchronously by
    // CreateTable, independent of the WAL/data-page crash simulation.
  }

  // --- Phase 2: reopen and recover. ---
  {
    DiskManager dm(db);
    BufferPool bpm(64, &dm);
    Catalog cat(&bpm, meta);
    RecoveryManager recovery(&dm, &bpm, &cat);
    int redone = recovery.Recover();
    assert(redone == 3);  // all 3 inserts replayed from WAL since nothing reached disk pre-crash

    Executor ex(&cat);
    auto r = Run(ex, "SELECT * FROM users");
    assert(r.ok);
    assert(r.rows.size() == 2);  // only alice and bob - carol's txn was a loser, undone

    bool has_alice = false, has_bob = false, has_carol = false;
    for (auto &row : r.rows) {
      if (row[1] == "alice") has_alice = true;
      if (row[1] == "bob") has_bob = true;
      if (row[1] == "carol") has_carol = true;
    }
    assert(has_alice && has_bob && !has_carol);
  }

  std::remove(db);
  std::remove((std::string(db) + ".wal").c_str());
  std::remove(meta);
  std::cout << "recovery_test PASSED (committed txns preserved, in-flight txn undone)\n";
  return 0;
}
