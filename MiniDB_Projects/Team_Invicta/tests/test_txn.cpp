// Transaction tests: lock compatibility, deterministic deadlock detection with
// youngest-victim abort, and serialized concurrent updates under exclusive
// locks (strict 2PL).
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
#include "txn/lock_manager.h"
#include "txn/transaction_manager.h"
#include "test_util.h"

using namespace minidb;

int main() {
  std::printf("test_txn\n");

  // --- 1. Shared locks are compatible; exclusive sets are tracked. ----------
  {
    LockManager lm;
    TransactionManager tm(&lm);
    Transaction *a = tm.Begin();
    Transaction *b = tm.Begin();
    RowId r{"t", 1};
    CHECK(lm.LockShared(a, r));
    CHECK(lm.LockShared(b, r));  // two readers coexist
    CHECK(a->HoldsShared(r));
    CHECK(b->HoldsShared(r));
    tm.Commit(a);
    tm.Commit(b);
  }

  // --- 2. Deadlock detection: cross lock A/B, youngest txn is aborted. ------
  {
    LockManager lm;
    TransactionManager tm(&lm);
    Transaction *t1 = tm.Begin();  // smaller id
    Transaction *t2 = tm.Begin();  // larger id => youngest => victim
    RowId A{"t", 100}, B{"t", 200};

    std::atomic<int> ready{0};
    std::atomic<int> aborted_id{-1};
    std::atomic<bool> t1_got_b{false};

    auto worker1 = [&]() {
      lm.LockExclusive(t1, A);
      ++ready;
      while (ready.load() < 2) {}
      try {
        lm.LockExclusive(t1, B);  // waits for t2; should succeed after t2 aborts
        t1_got_b = true;
      } catch (const TransactionAbortException &e) {
        aborted_id = static_cast<int>(e.txn_id);
      }
    };
    auto worker2 = [&]() {
      lm.LockExclusive(t2, B);
      ++ready;
      while (ready.load() < 2) {}
      try {
        lm.LockExclusive(t2, A);  // forms the cycle; t2 is youngest -> victim
      } catch (const TransactionAbortException &e) {
        aborted_id = static_cast<int>(e.txn_id);
        tm.Abort(t2);  // release t2's locks so t1 can proceed
      }
    };

    std::thread th1(worker1), th2(worker2);
    th1.join();
    th2.join();

    CHECK_EQ(aborted_id.load(), static_cast<int>(t2->id()));  // youngest aborted
    CHECK(t1_got_b.load());                                    // survivor proceeded
    tm.Commit(t1);
  }

  // --- 3. Serializability: concurrent increments under X locks are exact. ---
  {
    LockManager lm;
    TransactionManager tm(&lm);
    RowId acct{"bank", 0};
    long balance = 0;  // deliberately non-atomic: exclusivity must protect it
    const int THREADS = 8, PER = 500;

    auto bump = [&]() {
      for (int i = 0; i < PER; ++i) {
        Transaction *t = tm.Begin();
        lm.LockExclusive(t, acct);
        balance += 1;          // critical section guarded by the X lock
        tm.Commit(t);          // strict 2PL: lock released at commit
      }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < THREADS; ++i) ts.emplace_back(bump);
    for (auto &t : ts) t.join();

    CHECK_EQ(balance, static_cast<long>(THREADS) * PER);  // no lost updates
  }

  TEST_PASS();
}
