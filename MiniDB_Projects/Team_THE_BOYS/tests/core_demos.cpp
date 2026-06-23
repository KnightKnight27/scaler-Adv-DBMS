#include <iostream>

#include "transaction/lock_manager.h"

namespace {

bool TestConcurrentSharedLocks() {
    minidb::LockManager lm;
    if (!lm.Lock("users", minidb::LockMode::SHARED, 1)) return false;
    if (!lm.Lock("users", minidb::LockMode::SHARED, 2)) return false;
    if (lm.Lock("users", minidb::LockMode::EXCLUSIVE, 3)) return false;
    if (lm.LastFailureWasDeadlock()) return false;
    lm.UnlockAll(1);
    lm.UnlockAll(2);
    return lm.Lock("users", minidb::LockMode::EXCLUSIVE, 3);
}

bool TestDeadlockDetection() {
    minidb::LockManager lm;
    if (!lm.Lock("table_a", minidb::LockMode::EXCLUSIVE, 1)) return false;
    if (!lm.Lock("table_b", minidb::LockMode::EXCLUSIVE, 2)) return false;

    if (lm.Lock("table_b", minidb::LockMode::EXCLUSIVE, 1)) return false;
    if (lm.LastFailureWasDeadlock()) return false;

    if (lm.Lock("table_a", minidb::LockMode::EXCLUSIVE, 2)) return false;
    return lm.LastFailureWasDeadlock();
}

}  // namespace

int main() {
    int failed = 0;
    if (!TestConcurrentSharedLocks()) {
        std::cerr << "FAIL: concurrent shared locks\n";
        failed++;
    } else {
        std::cout << "PASS: concurrent shared locks\n";
    }
    if (!TestDeadlockDetection()) {
        std::cerr << "FAIL: deadlock detection\n";
        failed++;
    } else {
        std::cout << "PASS: deadlock detection\n";
    }
    return failed == 0 ? 0 : 1;
}
