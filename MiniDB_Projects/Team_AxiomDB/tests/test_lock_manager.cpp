#include "catch.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include "txn/concurrency_lock_manager.h"
#include "txn/transaction.h"

using namespace axiomdb;
using namespace std::chrono_literals;

TEST_CASE("shared locks are compatible", "[txn][lock]") {
  ConcurrencyLockManager lm;
  Transaction a(1), b(2);
  REQUIRE(lm.lock_shared(&a, 100));
  REQUIRE(lm.lock_shared(&b, 100));  // both hold S simultaneously, no blocking
  lm.unlock_all(&a);
  lm.unlock_all(&b);
}

TEST_CASE("exclusive lock blocks a conflicting acquirer until release", "[txn][lock]") {
  ConcurrencyLockManager lm;
  Transaction a(1), b(2);
  REQUIRE(lm.lock_exclusive(&a, 200));

  std::atomic<bool> b_got{false};
  std::thread t([&] {
    lm.lock_exclusive(&b, 200);  // must block while a holds X
    b_got = true;
  });

  std::this_thread::sleep_for(50ms);
  REQUIRE_FALSE(b_got.load());  // still blocked

  lm.unlock_all(&a);            // releasing lets b proceed
  t.join();
  REQUIRE(b_got.load());
  lm.unlock_all(&b);
}

TEST_CASE("S->X upgrade succeeds for the sole holder", "[txn][lock]") {
  ConcurrencyLockManager lm;
  Transaction a(1);
  REQUIRE(lm.lock_shared(&a, 300));
  REQUIRE(lm.lock_exclusive(&a, 300));  // upgrade in place
  lm.unlock_all(&a);
}

TEST_CASE("deadlock is detected and exactly one transaction is the victim", "[txn][lock][deadlock]") {
  ConcurrencyLockManager lm;
  Transaction t1(1), t2(2);
  std::atomic<bool> t1_has{false}, t2_has{false};
  std::atomic<int> victims{0};
  std::atomic<int> winners{0};

  auto cross = [&](Transaction* self, uint64_t first, uint64_t second,
                   std::atomic<bool>& mine, std::atomic<bool>& other) {
    REQUIRE(lm.lock_exclusive(self, first));
    mine = true;
    while (!other.load()) std::this_thread::sleep_for(1ms);  // ensure both hold their first
    if (lm.lock_exclusive(self, second)) {
      ++winners;
    } else {
      ++victims;  // chosen as deadlock victim
    }
    lm.unlock_all(self);
  };

  std::thread a([&] { cross(&t1, 1, 2, t1_has, t2_has); });
  std::thread b([&] { cross(&t2, 2, 1, t2_has, t1_has); });
  a.join();
  b.join();

  REQUIRE(victims.load() == 1);  // exactly one victim breaks the cycle
  REQUIRE(winners.load() == 1);  // the other ultimately succeeds
}
