#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

#include "txn/lock_manager.h"
#include "txn/transaction_manager.h"

using namespace minidb;

int main() {
  LockManager lm;
  TransactionManager tm(&lm);

  // --- Basic shared/exclusive compatibility ---
  {
    auto t1 = tm.Begin();
    auto t2 = tm.Begin();
    lm.LockShared(t1.get(), "users");
    lm.LockShared(t2.get(), "users");  // two readers: fine
    tm.Commit(t1.get());
    tm.Commit(t2.get());
  }

  // --- Deadlock: T1 locks A then waits for B; T2 locks B then waits for A ---
  std::atomic<int> aborted_count{0};
  std::atomic<int> committed_count{0};

  auto t1 = tm.Begin();
  auto t2 = tm.Begin();

  std::thread th1([&] {
    try {
      lm.LockExclusive(t1.get(), "A");
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      lm.LockExclusive(t1.get(), "B");
      tm.Commit(t1.get());
      committed_count++;
    } catch (const TransactionAbortedException &) {
      tm.Abort(t1.get());
      aborted_count++;
    }
  });
  std::thread th2([&] {
    try {
      lm.LockExclusive(t2.get(), "B");
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      lm.LockExclusive(t2.get(), "A");
      tm.Commit(t2.get());
      committed_count++;
    } catch (const TransactionAbortedException &) {
      tm.Abort(t2.get());
      aborted_count++;
    }
  });
  th1.join();
  th2.join();

  // The deadlock detector must resolve the cycle: exactly one side aborts,
  // the other proceeds to commit.
  assert(aborted_count == 1);
  assert(committed_count == 1);

  std::cout << "txn_test PASSED (deadlock resolved: " << aborted_count << " aborted, "
            << committed_count << " committed)\n";
  return 0;
}
