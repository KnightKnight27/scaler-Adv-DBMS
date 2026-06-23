#include <barrier>
#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "minidb/common/trace.h"
#include "minidb/transaction/lock_manager.h"

int main() {
  using namespace std::chrono_literals;
  minidb::Trace::SetEnabled(true);
  minidb::LockManager locks;
  std::barrier sync(2);

  auto worker1 = [&] {
    auto txn = locks.Begin();
    try {
      std::cout << "Worker 1 / DB T" << txn->Id() << ": lock resource A\n";
      locks.LockExclusive(*txn, "A");
      sync.arrive_and_wait();
      std::this_thread::sleep_for(50ms);
      std::cout << "Worker 1 / DB T" << txn->Id()
                << ": now requests B and may wait\n";
      locks.LockExclusive(*txn, "B");
      locks.Commit(*txn);
      std::cout << "Worker 1 / DB T" << txn->Id() << ": committed\n";
    } catch (const std::exception &error) {
      std::cout << "Worker 1 / DB T" << txn->Id() << ": " << error.what()
                << '\n';
      locks.Abort(*txn);
    }
  };

  auto worker2 = [&] {
    auto txn = locks.Begin();
    try {
      std::cout << "Worker 2 / DB T" << txn->Id() << ": lock resource B\n";
      locks.LockExclusive(*txn, "B");
      sync.arrive_and_wait();
      std::this_thread::sleep_for(50ms);
      std::cout << "Worker 2 / DB T" << txn->Id()
                << ": now requests A, creating a waits-for cycle\n";
      locks.LockExclusive(*txn, "A");
      locks.Commit(*txn);
      std::cout << "Worker 2 / DB T" << txn->Id() << ": committed\n";
    } catch (const std::exception &error) {
      std::cout << "Worker 2 / DB T" << txn->Id() << ": " << error.what()
                << '\n';
      locks.Abort(*txn);
    }
  };

  std::thread t1(worker1);
  std::thread t2(worker2);
  t1.join();
  t2.join();
  std::cout << "Transaction demo complete: conflict, waiting, and deadlock detection were exercised.\n";
  return 0;
}
