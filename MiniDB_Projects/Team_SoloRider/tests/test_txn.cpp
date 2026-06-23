#include <catch2/catch_test_macros.hpp>
#include "txn/lock_manager.h"
#include "txn/deadlock_detector.h"
#include <thread>
#include <chrono>

using namespace minidb;

TEST_CASE("LockManager: Basic Locking", "[txn]") {
    LockManager lm;
    Transaction t1(1);
    Transaction t2(2);
    RecordId rid{1, 1};
    
    REQUIRE(lm.lock_shared(&t1, rid) == true);
    REQUIRE(lm.lock_shared(&t2, rid) == true); // S and S are compatible
    
    REQUIRE(lm.unlock(&t1, rid) == true);
    REQUIRE(lm.unlock(&t2, rid) == true);
}

TEST_CASE("LockManager: Deadlock Detection", "[txn]") {
    LockManager lm;
    Transaction t1(1);
    Transaction t2(2);
    RecordId rid1{1, 1};
    RecordId rid2{1, 2};
    
    REQUIRE(lm.lock_exclusive(&t1, rid1) == true);
    REQUIRE(lm.lock_exclusive(&t2, rid2) == true);
    
    // Create deadlock in background
    std::thread t1_thread([&]() {
        lm.lock_exclusive(&t1, rid2);
    });
    std::thread t2_thread([&]() {
        // Sleep to ensure t1 requests rid2 first
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        lm.lock_exclusive(&t2, rid1);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    DeadlockDetector detector(&lm);
    int abort_txn = detector.detect_deadlock();
    REQUIRE(abort_txn == 2); // t2 has higher ID
    
    if (abort_txn == 1) lm.abort_txn(&t1);
    else if (abort_txn == 2) lm.abort_txn(&t2);
    
    t1_thread.join();
    t2_thread.join();
    
    REQUIRE(t2.get_state() == TransactionState::ABORTED);
}
