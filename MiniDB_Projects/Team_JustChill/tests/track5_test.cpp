// Storage-integration tests for Phases A, B, D, E:
//   - slotted page layout + tuple (de)serialization
//   - page-backed heap table (scan, index, eviction) and persistence
//   - page-backed B+ tree persistence across reopen
//   - transactions: auto-commit + undo-based rollback
//   - WAL recovery: replay only committed transactions
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "execution.h"
#include "page.h"
#include "slotted_page.h"
#include "transaction.h"
#include "wal.h"

namespace fs = std::filesystem;
using namespace minidb;

static int g_checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      std::cerr << "FAIL: " << #cond << " @ line " << __LINE__ << "\n"; \
      std::exit(1);                                                     \
    }                                                                   \
  } while (0)

static void rmTable(const std::string& name) {
  std::error_code ec;
  fs::remove(name + ".dat", ec);
  fs::remove(name + ".idx", ec);
}

// ---- Phase A1: slotted page ----
static void testSlottedPage() {
  Page page;  // zeroed 4 KB frame
  SlottedPage sp(page.data);
  CHECK(sp.numSlots() == 0);
  CHECK(sp.freeEnd() == PAGE_SIZE);

  const char* a = "hello";
  const char* b = "world!!";
  int s0 = sp.insertRecord(a, 5);
  int s1 = sp.insertRecord(b, 7);
  CHECK(s0 == 0 && s1 == 1);
  CHECK(sp.numSlots() == 2);

  const char* rec = nullptr;
  uint16_t len = 0;
  CHECK(sp.getRecord(0, rec, len) && len == 5 && std::memcmp(rec, a, 5) == 0);
  CHECK(sp.getRecord(1, rec, len) && len == 7 && std::memcmp(rec, b, 7) == 0);

  int free_before = sp.freeSpace();
  sp.deleteRecord(0);  // tombstone
  CHECK(sp.getRecord(0, rec, len) && len == 0);
  CHECK(sp.freeSpace() == free_before);  // no compaction on delete

  sp.restoreRecord(0, 5);  // undo of delete
  CHECK(sp.getRecord(0, rec, len) && len == 5 && std::memcmp(rec, a, 5) == 0);

  // Re-reading the same bytes through a new view sees the same data (layout is
  // entirely in the page buffer, no external state).
  SlottedPage sp2(page.data);
  CHECK(sp2.numSlots() == 2);
  std::cout << "[ok] SlottedPage: insert/get/delete/restore + persistence\n";
}

// ---- Phase A2: tuple serialization ----
static void testSerialization() {
  Schema schema = {{"id", ValueType::Int}, {"name", ValueType::Text}};
  Tuple t = {Value::Int(-42), Value::Text("Grace Hopper")};
  std::vector<char> bytes = serializeTuple(t);
  Tuple back = deserializeTuple(bytes.data(), bytes.size(), schema);
  CHECK(back.size() == 2);
  CHECK(back[0].type == ValueType::Int && back[0].i == -42);
  CHECK(back[1].type == ValueType::Text && back[1].s == "Grace Hopper");

  // empty string round-trips
  Tuple t2 = {Value::Int(0), Value::Text("")};
  auto b2 = serializeTuple(t2);
  Tuple back2 = deserializeTuple(b2.data(), b2.size(), schema);
  CHECK(back2[1].s.empty());
  std::cout << "[ok] Serialization: int/text round-trip\n";
}

// ---- Phase A3/A4 + B3: page-backed table, scan, index ----
static void testPageBackedTable() {
  rmTable("t5_basic");
  Catalog cat;
  Table* t = cat.createTable("t5_basic",
                             {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);
  ExecContext ctx;  // no lock manager (unit-test mode)

  for (int i = 1; i <= 100; ++i)
    t->insert({Value::Int(i), Value::Text("name" + std::to_string(i))});
  CHECK(t->size() == 100);

  // Sequential scan returns every row.
  {
    TableScan scan(t, ctx);
    auto rows = execute(scan);
    CHECK(rows.size() == 100);
  }
  // Index point lookups (page-backed B+ tree).
  CHECK(t->index().search(1).has_value());
  CHECK(t->index().search(100).has_value());
  CHECK(!t->index().search(101).has_value());

  // Index range scan via IndexScan operator.
  {
    IndexScan iscan(t, 40, 49, ctx);
    auto rows = execute(iscan);
    CHECK(rows.size() == 10);
    CHECK(rows.front()[0].i == 40 && rows.back()[0].i == 49);
  }

  // Delete via the operator path; row vanishes from scan and index.
  {
    auto scan = std::make_unique<TableScan>(t, ctx);
    auto filt = std::make_unique<Filter>(
        std::move(scan), Predicate{0, CompareOp::Eq, Value::Int(50)});
    Delete del(t, std::move(filt), ctx);
    execute(del);
    CHECK(del.deleted() == 1);
    CHECK(!t->index().search(50).has_value());
    TableScan verify(t, ctx);
    CHECK(execute(verify).size() == 99);
  }
  rmTable("t5_basic");
  std::cout << "[ok] Page-backed Table: insert/scan/index/range/delete\n";
}

// ---- Buffer-pool eviction during a scan ----
static void testEviction() {
  rmTable("t5_evict");
  Catalog cat;
  Table* t = cat.createTable("t5_evict",
                             {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);
  ExecContext ctx;
  // ~200-byte rows => ~19 rows/page; 1500 rows => ~79 data pages, far more than
  // the 64-frame data pool, so the scan must drive eviction and still be correct.
  std::string pad(200, 'x');
  const int N = 1500;
  for (int i = 0; i < N; ++i) t->insert({Value::Int(i), Value::Text(pad)});
  CHECK(t->numPages() > 64);  // proves more pages than buffer frames

  TableScan scan(t, ctx);
  auto rows = execute(scan);
  CHECK(static_cast<int>(rows.size()) == N);
  CHECK(t->index().search(0).has_value() && t->index().search(N - 1).has_value());
  rmTable("t5_evict");
  std::cout << "[ok] BufferPool: eviction during scan (" << t->numPages()
            << " pages / 64 frames)\n";
}

// ---- Persistence: data + index survive a close/reopen ----
static void testPersistence() {
  rmTable("t5_persist");
  {
    Catalog cat;
    Table* t = cat.createTable(
        "t5_persist", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0,
        /*fresh=*/true);
    for (int i = 1; i <= 200; ++i)
      t->insert({Value::Int(i), Value::Text("v" + std::to_string(i))});
    // Catalog/Table destroyed here -> buffer pools flush dirty pages to disk.
  }
  {
    Catalog cat;
    Table* t = cat.createTable(
        "t5_persist", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0,
        /*fresh=*/false);  // reopen durable state
    CHECK(t->size() == 200);
    CHECK(t->index().search(1).has_value());    // page-backed index persisted
    CHECK(t->index().search(123).has_value());
    CHECK(t->index().search(200).has_value());
    ExecContext ctx;
    TableScan scan(t, ctx);
    auto rows = execute(scan);
    CHECK(rows.size() == 200);
  }
  rmTable("t5_persist");
  std::cout << "[ok] Persistence: heap + B+ tree survive reopen\n";
}

// ---- Phase D: transactions (commit keeps, abort rolls back) ----
static void testTransactions() {
  rmTable("t5_txn");
  Catalog cat;
  Table* t = cat.createTable("t5_txn",
                             {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);
  LockManager lm;
  TransactionManager tm;

  auto runInsert = [&](int id, bool commit) {
    Transaction* txn = tm.begin();
    ExecContext ctx{&lm, txn->id(), txn};
    Insert ins(t, {Value::Int(id), Value::Text("x")}, ctx);
    execute(ins);
    if (commit) {
      tm.commit(txn);
    } else {
      tm.abort(txn);
    }
    lm.release_all(txn->id());
  };

  runInsert(1, /*commit=*/true);
  CHECK(t->index().search(1).has_value());

  runInsert(2, /*commit=*/false);            // inserted then aborted
  CHECK(!t->index().search(2).has_value());  // rolled back

  // Delete inside a transaction, then abort -> the row comes back.
  {
    Transaction* txn = tm.begin();
    ExecContext ctx{&lm, txn->id(), txn};
    auto scan = std::make_unique<TableScan>(t, ctx);
    auto filt = std::make_unique<Filter>(
        std::move(scan), Predicate{0, CompareOp::Eq, Value::Int(1)});
    Delete del(t, std::move(filt), ctx);
    execute(del);
    CHECK(del.deleted() == 1);
    CHECK(!t->index().search(1).has_value());  // gone within the txn
    tm.abort(txn);
    lm.release_all(txn->id());
  }
  CHECK(t->index().search(1).has_value());  // delete rolled back

  CHECK(tm.state(1) == TxnState::Committed);
  CHECK(tm.state(2) == TxnState::Aborted);
  rmTable("t5_txn");
  std::cout << "[ok] Transactions: auto-commit + undo rollback (insert/delete)\n";
}

// ---- Phase E: WAL recovery replays only committed transactions ----
static void testWalRecovery() {
  std::string wal = "t5_recovery.log";
  std::error_code ec;
  fs::remove(wal, ec);
  {
    LogManager lm(wal);
    lm.logBegin(1);
    lm.logStatement(1, "INSERT INTO students VALUES (1, 'a')");
    lm.logCommit(1);

    lm.logBegin(2);  // crash mid-transaction: no COMMIT
    lm.logStatement(2, "INSERT INTO students VALUES (2, 'b')");

    lm.logBegin(3);
    lm.logStatement(3, "DELETE FROM students WHERE id = 1");
    lm.logCommit(3);
  }

  auto stmts = committedStatements(wal);
  CHECK(stmts.size() == 2);  // txn 2 dropped (uncommitted)
  CHECK(stmts[0] == "INSERT INTO students VALUES (1, 'a')");
  CHECK(stmts[1] == "DELETE FROM students WHERE id = 1");
  fs::remove(wal, ec);
  std::cout << "[ok] WAL recovery: only committed transactions replayed\n";
}

int main() {
  testSlottedPage();
  testSerialization();
  testPageBackedTable();
  testEviction();
  testPersistence();
  testTransactions();
  testWalRecovery();
  std::cout << "\nAll Track 5 (storage integration) checks passed (" << g_checks
            << " assertions).\n";
  return 0;
}
