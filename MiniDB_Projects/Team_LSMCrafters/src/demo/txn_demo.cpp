// Demonstrates the transaction layer: shared/exclusive lock blocking, that two
// readers do not block each other, and deadlock detection with one victim.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_table.h"
#include "txn/lock_manager.h"
#include "txn/txn_manager.h"
#include "txn/wal.h"

using namespace minidb;
using namespace std::chrono_literals;

static void demo_blocking(TransactionManager& tm) {
  std::cout << "=== Demo 1: exclusive locks block, shared locks share ===\n";
  TxnId t1 = tm.begin();
  tm.write(t1, 1, "100");
  std::cout << "T1 took an X lock on row 1 (wrote 100)\n";

  std::thread reader([&] {
    TxnId t2 = tm.begin();
    std::cout << "T2 requests an S lock on row 1 -> blocks (X held by T1)\n";
    auto value = tm.read(t2, 1);
    std::cout << "T2 unblocked, read row 1 = " << (value ? *value : "<none>") << "\n";
    tm.commit(t2);
  });

  std::this_thread::sleep_for(200ms);  // give T2 time to block
  std::cout << "T1 commits, releasing its lock\n";
  tm.commit(t1);
  reader.join();

  TxnId a = tm.begin(), b = tm.begin();
  tm.read(a, 1);
  tm.read(b, 1);
  std::cout << "Two S locks on row 1 both granted -> readers don't block readers\n";
  tm.commit(a);
  tm.commit(b);
}

static void demo_deadlock(TransactionManager& tm) {
  std::cout << "\n=== Demo 2: deadlock detection ===\n";
  TxnId ta = tm.begin(), tb = tm.begin();
  tm.write(ta, 10, "A");
  tm.write(tb, 20, "B");
  std::cout << "TA holds X on row 10, TB holds X on row 20\n";

  std::atomic<bool> ta_aborted{false}, tb_aborted{false};
  std::thread th([&] {
    try {
      tm.write(ta, 20, "A2");  // wants row 20 (held by TB)
      tm.commit(ta);
      std::cout << "TA committed\n";
    } catch (const TxnAbortException&) {
      ta_aborted = true;
      tm.abort(ta);
      std::cout << "TA chosen as deadlock victim and aborted\n";
    }
  });

  try {
    tm.write(tb, 10, "B2");  // wants row 10 (held by TA) -> closes the cycle
    tm.commit(tb);
    std::cout << "TB committed\n";
  } catch (const TxnAbortException&) {
    tb_aborted = true;
    tm.abort(tb);
    std::cout << "TB chosen as deadlock victim and aborted\n";
  }
  th.join();

  std::cout << "Result: exactly one victim aborted = "
            << ((ta_aborted.load() ^ tb_aborted.load()) ? "yes" : "NO") << "\n";
}

int main() {
  std::remove("/tmp/minidb_txn.db");
  std::remove("/tmp/minidb_txn.wal");

  DiskManager disk("/tmp/minidb_txn.db");
  BufferPool pool(disk);
  std::unique_ptr<HeapTable> table = make_heap_table(pool);
  LockManager locks;
  LogManager log("/tmp/minidb_txn.wal", /*truncate=*/true);
  TransactionManager tm(locks, log, *table);

  demo_blocking(tm);
  demo_deadlock(tm);
  return 0;
}
