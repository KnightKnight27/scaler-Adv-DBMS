#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"

using namespace minidb;

class TransactionTest : public ::testing::Test {
protected:
    LockManager lock_manager;
    TransactionManager txn_manager{&lock_manager};
};

TEST_F(TransactionTest, ConcurrentLockAcquisition) {
    Transaction* txn1 = txn_manager.Begin();
    Transaction* txn2 = txn_manager.Begin();

    // 1. txn1 exclusively locks Row A
    EXPECT_TRUE(lock_manager.LockExclusive(txn1, "1_rowA"));

    std::atomic<bool> txn2_acquired_lock{false};

    // 2. Spawn a background thread where txn2 tries to lock Row A concurrently.
    // Because txn1 holds the exclusive lock, txn2 MUST block (Strict 2PL rule).
    std::thread background_thread([&]() {
        bool success = lock_manager.LockExclusive(txn2, "1_rowA");
        EXPECT_TRUE(success);
        txn2_acquired_lock = true;
    });

    // Give the background thread time to execute and block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 3. VERIFY: txn2 should still be waiting, unable to acquire the lock
    EXPECT_FALSE(txn2_acquired_lock.load()); 

    // 4. txn1 Commits (Shrinking Phase). This officially releases the lock on Row A and wakes up txn2.
    txn_manager.Commit(txn1);

    // The background thread should now automatically wake up and acquire Row A.
    background_thread.join();
    
    // 5. VERIFY: txn2 successfully acquired the lock immediately after txn1 committed
    EXPECT_TRUE(txn2_acquired_lock.load());

    // 6. txn2 Commits, safely releasing its locks
    txn_manager.Commit(txn2);
}