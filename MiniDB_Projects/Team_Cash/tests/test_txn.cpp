// Tests for 2PL lock manager: compatibility, conflicts, deadlock detection.
#include "check.h"
#include "txn.h"

using namespace minidb;

int main() {
    // shared locks are compatible; an exclusive request conflicts with them
    {
        LockManager lm;
        CHECK(lm.acquire(1, 100, LockMode::Shared) == LockManager::Granted);
        CHECK(lm.acquire(2, 100, LockMode::Shared) == LockManager::Granted);
        CHECK(lm.acquire(3, 100, LockMode::Exclusive) == LockManager::Waiting);
        lm.release(1);
        lm.release(2);
        // now no shared holders remain; T3 can take the exclusive lock
        CHECK(lm.acquire(3, 100, LockMode::Exclusive) == LockManager::Granted);
    }

    // a two-transaction cycle is detected as a deadlock
    {
        LockManager lm;
        CHECK(lm.acquire(1, 1, LockMode::Exclusive) == LockManager::Granted);
        CHECK(lm.acquire(2, 2, LockMode::Exclusive) == LockManager::Granted);
        CHECK(lm.acquire(1, 2, LockMode::Exclusive) == LockManager::Waiting);   // T1 waits for T2
        CHECK(lm.acquire(2, 1, LockMode::Exclusive) == LockManager::Deadlock);  // closes the cycle
    }

    REPORT();
}
