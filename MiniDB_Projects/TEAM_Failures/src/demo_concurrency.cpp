// ============================================================================
// demo_concurrency.cpp  --  Demonstrates the transaction layer with real
// threads: Strict 2PL locking (one txn BLOCKS until another commits) and
// DEADLOCK detection (a cycle is detected and one txn is aborted).
//
// runSql:  ./build/demo
// ============================================================================
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include "engine/database.h"
#include "sql/parser.h"

using namespace minidb;
using namespace chrono;

static mutex print_mtx;
static steady_clock::time_point START;

// Thread-safe, timestamped logging so the interleaving is easy to read.
static void logMsg(const string &who, const string &msg) {
  lock_guard<mutex> g(print_mtx);
  long t = duration_cast<milliseconds>(steady_clock::now() - START).count();
  cout << "[t+" << t << "ms] " << who << ": " << msg << "\n" << flush;
}

static void runSql(Database &db, Transaction *txn, const string &sql) {
  auto stmt = Parser::parse(sql);
  db.execute(stmt.get(), txn);
}

// ---------------------------------------------------------------------------
// Demo 1: a writer holds an exclusive lock; a reader must WAIT for the commit.
// ---------------------------------------------------------------------------
static void demoBlocking(Database &db) {
  cout << "\n=== Demo 1: lock blocking (Strict 2PL) ===\n";
  START = steady_clock::now();

  thread writer([&] {
    Transaction *t = db.begin();
    logMsg("T1(writer)", "BEGIN, acquiring X lock on accounts via INSERT");
    runSql(db, t, "INSERT INTO accounts VALUES (3, 300);");
    logMsg("T1(writer)", "holding X lock; working for 600ms...");
    this_thread::sleep_for(milliseconds(600));
    db.commit(t);
    logMsg("T1(writer)", "COMMIT -> releases locks");
  });

  thread reader([&] {
    this_thread::sleep_for(milliseconds(150));   // ensure T1 locks first
    Transaction *t = db.begin();
    logMsg("T2(reader)", "BEGIN, wants S lock on accounts (SELECT) -> should BLOCK");
    runSql(db, t, "SELECT * FROM accounts;");
    logMsg("T2(reader)", "got the lock (T1 had committed) -> SELECT ran");
    db.commit(t);
  });

  writer.join();
  reader.join();
}

// ---------------------------------------------------------------------------
// Demo 2: two txns lock tables in opposite order -> deadlock -> one is aborted.
// ---------------------------------------------------------------------------
static void demoDeadlock(Database &db) {
  cout << "\n=== Demo 2: deadlock detection ===\n";
  START = steady_clock::now();

  mutex m; condition_variable cv; int ready = 0;
  auto wait_both = [&] {                       // simple 2-thread barrier
    unique_lock<mutex> lk(m);
    if (++ready == 2) cv.notify_all();
    else cv.wait(lk, [&] { return ready == 2; });
  };

  auto worker = [&](const string &who, const string &first,
                    const string &second) {
    Transaction *t = db.begin();
    try {
      runSql(db, t, first);
      logMsg(who, "locked its first table");
      wait_both();                              // both hold one lock, now cross
      logMsg(who, "now requesting the other table's lock...");
      runSql(db, t, second);
      db.commit(t);
      logMsg(who, "COMMIT (this txn won)");
    } catch (const DBException &e) {
      logMsg(who, string("ABORTED -> ") + e.what());
      db.abort(t);
    }
  };

  thread a(worker, "T1", "INSERT INTO A VALUES (1, 1);", "INSERT INTO B VALUES (1, 1);");
  thread b(worker, "T2", "INSERT INTO B VALUES (2, 2);", "INSERT INTO A VALUES (2, 2);");
  a.join();
  b.join();
}

int main() {
  remove("demo_cc.db"); remove("demo_cc.wal"); remove("demo_cc.catalog");
  Database db("demo_cc");

  // Set up tables (single-threaded, auto-commit).
  db.executeAutoCommit("CREATE TABLE accounts (id INT PRIMARY KEY, bal INT);");
  db.executeAutoCommit("INSERT INTO accounts VALUES (1, 100);");
  db.executeAutoCommit("INSERT INTO accounts VALUES (2, 200);");
  db.executeAutoCommit("CREATE TABLE A (id INT PRIMARY KEY, v INT);");
  db.executeAutoCommit("CREATE TABLE B (id INT PRIMARY KEY, v INT);");

  demoBlocking(db);
  demoDeadlock(db);

  cout << "\nDone. (One deadlocked transaction was aborted; the other committed.)\n";
  remove("demo_cc.db"); remove("demo_cc.wal"); remove("demo_cc.catalog");
  return 0;
}
