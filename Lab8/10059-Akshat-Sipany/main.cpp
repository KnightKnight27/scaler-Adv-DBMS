#include "transaction_manager.h"
#include <chrono>
#include <iostream>
#include <thread>

static void display_read_result(const std::optional<std::string> &result,
                                TransactionID tid, const RecordKey &rk) {
  std::cout << "  [TX " << tid << "] READ " << rk << " = "
            << (result.has_value() ? result.value() : "<not visible>") << "\n";
}

int main() {
  TransactionManager mgr;

  std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
  {
    TransactionID w1 = mgr.begin();
    mgr.insert(w1, "balance", "1000");
    mgr.commit(w1);

    TransactionID r1 = mgr.begin();
    TransactionID w2 = mgr.begin();

    mgr.update(w2, "balance", "2000");
    mgr.commit(w2);

    auto val = mgr.read(r1, "balance");
    display_read_result(val, r1, "balance"); // expected: 1000
    mgr.commit(r1);
  }

  std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
  {
    TransactionID ra = mgr.begin();
    TransactionID rb = mgr.begin();

    display_read_result(mgr.read(ra, "balance"), ra, "balance");
    display_read_result(mgr.read(rb, "balance"), rb, "balance");

    mgr.commit(ra);
    mgr.commit(rb);
  }

  std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
  {
    TransactionID wx = mgr.begin();
    mgr.update(wx, "balance", "3000");

    std::thread blocked_reader([&]() {
      TransactionID rx = mgr.begin();
      std::cout << "  [TX " << rx
                << "] waiting for shared lock on balance...\n";
      auto v = mgr.read(rx, "balance");

      mgr.commit(rx);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    blocked_reader.join();
  }

  std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
  {
    TransactionID sa = mgr.begin();
    TransactionID sb = mgr.begin();
    mgr.insert(sa, "A", "val_a");
    mgr.insert(sb, "B", "val_b");
    mgr.commit(sa);
    mgr.commit(sb);

    TransactionID x1 = mgr.begin();
    TransactionID x2 = mgr.begin();

    mgr.acquire_lock("A", x1, LockType::WRITE_LOCK);
    mgr.acquire_lock("B", x2, LockType::WRITE_LOCK);

    std::thread contender([&]() {
      try {
        mgr.acquire_lock("B", x1, LockType::WRITE_LOCK);
        mgr.commit(x1);
      } catch (CycleDetectedException &ex) {
        std::cout << "  " << ex.what() << "\n";
        mgr.abort(x1);
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    try {
      mgr.acquire_lock("A", x2, LockType::WRITE_LOCK);
      mgr.commit(x2);
    } catch (CycleDetectedException &ex) {
      std::cout << "  " << ex.what() << "\n";
      mgr.abort(x2);
    }

    contender.join();
  }

  std::cout << "\nAll scenarios complete.\n";
  return 0;
}