// Concurrency control: 2PL lock compatibility, blocking under contention, and
// deadlock detection (the youngest transaction is aborted to break the cycle).
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "txn/transaction_manager.h"

using namespace minidb;

// Two transactions acquire locks in opposite order -> classic deadlock. The
// lock manager must abort exactly one of them so the other can finish.
static void TestDeadlock() {
  TransactionManager tm;
  txn_id_t t1 = tm.Begin();  // older
  txn_id_t t2 = tm.Begin();  // younger -> expected victim

  tm.LockExclusive(t1, "A");
  tm.LockExclusive(t2, "B");

  std::atomic<int> aborts{0}, commits{0};

  std::thread th1([&] {
    try {
      tm.LockExclusive(t1, "B");  // waits for t2
      commits++;
      tm.Commit(t1);
    } catch (const TxnAborted&) { aborts++; tm.Abort(t1); }
  });
  std::thread th2([&] {
    try {
      tm.LockExclusive(t2, "A");  // waits for t1 -> cycle
      commits++;
      tm.Commit(t2);
    } catch (const TxnAborted&) { aborts++; tm.Abort(t2); }
  });
  th1.join();
  th2.join();

  // Exactly one victim; the other makes progress.
  assert(aborts == 1);
  assert(commits == 1);
  std::cout << "[txn] deadlock detected and broken (1 abort, 1 commit)\n";
}

// A shared lock lets multiple readers in; an exclusive request blocks until the
// readers release. We observe the writer waiting, then proceeding.
static void TestSharedExclusive() {
  TransactionManager tm;
  txn_id_t r1 = tm.Begin(), r2 = tm.Begin(), w = tm.Begin();

  tm.LockShared(r1, "X");
  tm.LockShared(r2, "X");  // two shared locks coexist -> no block

  std::atomic<bool> writer_in{false};
  std::thread tw([&] {
    tm.LockExclusive(w, "X");  // must wait for both readers
    writer_in = true;
    tm.Commit(w);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  assert(!writer_in);  // still blocked by the readers
  tm.Commit(r1);
  tm.Commit(r2);       // last reader released -> writer proceeds
  tw.join();
  assert(writer_in);
  std::cout << "[txn] shared coexist + exclusive blocks-then-proceeds OK\n";
}

int main() {
  TestSharedExclusive();
  TestDeadlock();
  std::cout << "[txn] OK\n";
  return 0;
}
