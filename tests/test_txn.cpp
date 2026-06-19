// Concurrency control: 2PL lock manager (wait-die) + transaction manager.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "common/exception.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "engine/database.h"
#include "tests/test_util.h"

using namespace minidb;

// Shared locks are mutually compatible — two readers proceed without blocking.
static void TestSharedCompatible() {
  LockManager lm;
  Transaction t1(1), t2(2);
  RID a(1, 1);
  lm.LockShared(&t1, a);
  lm.LockShared(&t2, a);
  CHECK(t1.LockSet().count(a) == 1);
  CHECK(t2.LockSet().count(a) == 1);
  lm.UnlockAll(&t1);
  lm.UnlockAll(&t2);
}

// Wait-die: a younger transaction that hits a conflicting lock held by an older
// one is aborted immediately (deadlock prevention).
static void TestWaitDieAbortsYounger() {
  LockManager lm;
  Transaction older(1), younger(2);
  RID a(1, 1);
  lm.LockExclusive(&older, a);
  bool aborted = false;
  try {
    lm.LockExclusive(&younger, a);
  } catch (const Exception &e) {
    aborted = true;
    CHECK(e.kind() == ErrorKind::kAbort);
  }
  CHECK(aborted);
  CHECK(younger.GetState() == TxnState::kAborted);
  lm.UnlockAll(&older);
}

// Wait-die: an older transaction blocks (waits) for a younger holder, then is
// granted once the younger releases. Demonstrates real concurrent blocking.
static void TestOlderWaitsForYounger() {
  LockManager lm;
  Transaction older(1), younger(2);
  RID a(1, 1);
  lm.LockExclusive(&younger, a);  // younger holds the lock

  std::atomic<bool> granted{false};
  std::thread waiter([&] {
    lm.LockExclusive(&older, a);  // older must wait, not die
    granted = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(!granted.load());  // still blocked while younger holds it

  lm.UnlockAll(&younger);  // release -> older proceeds
  waiter.join();
  CHECK(granted.load());
  lm.UnlockAll(&older);
}

// Two threads mutating a shared counter, each under an exclusive lock on the
// same RID. 2PL serializes the critical section so no updates are lost. Under
// wait-die a younger contender may be chosen as a victim and abort — the
// standard response is to retry, which is exactly what a real client does.
static void TestSerializedExclusiveAccess() {
  LockManager lm;
  TransactionManager tm(&lm);
  RID a(7, 7);
  int counter = 0;
  const int kPerThread = 1000;

  auto worker = [&] {
    for (int i = 0; i < kPerThread; i++) {
      while (true) {  // retry until this increment commits
        Transaction *t = tm.Begin();
        try {
          lm.LockExclusive(t, a);
          int v = counter;
          v++;
          counter = v;
          tm.Commit(t);  // releases the lock
          break;
        } catch (const Exception &) {
          tm.Abort(t);  // wait-die victim: release and retry
        }
      }
    }
  };
  std::thread t1(worker), t2(worker);
  t1.join();
  t2.join();
  CHECK_EQ(counter, 2 * kPerThread);  // no lost updates despite contention
}

// Begin/Commit lifecycle: locks held during the transaction, released at commit.
static void TestTxnLifecycle() {
  LockManager lm;
  TransactionManager tm(&lm);
  Transaction *t = tm.Begin();
  CHECK(t->GetState() == TxnState::kGrowing);
  RID a(2, 3);
  lm.LockExclusive(t, a);
  CHECK(t->LockSet().count(a) == 1);
  tm.Commit(t);
  CHECK(t->GetState() == TxnState::kCommitted);
  CHECK(t->LockSet().empty());
}

// SQL-level transactions: ROLLBACK undoes inserts and deletes (including the
// index), COMMIT persists across a reopen.
static void TestSqlTransactions() {
  const std::string f = "test_txn_sql.db";
  std::remove(f.c_str());
  {
    Database db(f);
    db.Execute("CREATE TABLE t (id INT, name VARCHAR(16))");
    db.Execute("CREATE INDEX t_id ON t (id)");
    db.Execute("INSERT INTO t VALUES (1,'a'),(2,'b')");
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 2);

    // ROLLBACK undoes inserts.
    db.Execute("BEGIN");
    db.Execute("INSERT INTO t VALUES (3,'c'),(4,'d')");
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 4);  // visible inside the txn
    db.Execute("ROLLBACK");
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 2);  // rolled back
    CHECK_EQ(db.Execute("SELECT id FROM t WHERE id = 3").affected, 0);

    // ROLLBACK undoes a delete (row + index entry restored).
    db.Execute("BEGIN");
    db.Execute("DELETE FROM t WHERE id = 1");
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 1);
    db.Execute("ROLLBACK");
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 2);
    CHECK_EQ(db.Execute("SELECT id FROM t WHERE id = 1").affected, 1);

    // COMMIT persists.
    db.Execute("BEGIN");
    db.Execute("INSERT INTO t VALUES (5,'e')");
    db.Execute("COMMIT");
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 3);
  }
  // Committed data survives reopen; rolled-back data does not.
  {
    Database db(f);
    CHECK_EQ(db.Execute("SELECT id FROM t").affected, 3);
    CHECK_EQ(db.Execute("SELECT id FROM t WHERE id = 5").affected, 1);
    CHECK_EQ(db.Execute("SELECT id FROM t WHERE id = 3").affected, 0);
  }
  std::remove(f.c_str());
}

int main() {
  TestSharedCompatible();
  TestWaitDieAbortsYounger();
  TestOlderWaitsForYounger();
  TestSerializedExclusiveAccess();
  TestTxnLifecycle();
  TestSqlTransactions();
  return minidb::test::summary("txn");
}
