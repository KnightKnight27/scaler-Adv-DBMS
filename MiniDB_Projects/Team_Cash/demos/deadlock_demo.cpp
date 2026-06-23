// Demonstrates 2PL lock acquisition and deadlock detection on a classic
// two-transaction cycle. Build: see the Makefile target `deadlock_demo`.
#include <algorithm>
#include <iostream>

#include "txn.h"

using namespace minidb;

static const char* name(LockManager::Outcome o) {
    return o == LockManager::Granted ? "GRANTED" : o == LockManager::Waiting ? "WAITING" : "DEADLOCK";
}

int main() {
    TransactionManager tm;
    LockManager& lm = tm.locks();
    int t1 = tm.begin();  // older
    int t2 = tm.begin();  // younger

    const int64_t A = 100, B = 200;
    std::cout << "T" << t1 << " requests X(A): " << name(lm.acquire(t1, A, LockMode::Exclusive)) << "\n";
    std::cout << "T" << t2 << " requests X(B): " << name(lm.acquire(t2, B, LockMode::Exclusive)) << "\n";
    std::cout << "T" << t1 << " requests X(B): " << name(lm.acquire(t1, B, LockMode::Exclusive))
              << "   (T" << t1 << " now waits for T" << t2 << ")\n";
    auto last = lm.acquire(t2, A, LockMode::Exclusive);
    std::cout << "T" << t2 << " requests X(A): " << name(last) << "   (would close a cycle)\n";

    std::cout << "waits-for graph:\n" << lm.waitsForGraph();

    if (last == LockManager::Deadlock) {
        int victim = std::max(t1, t2);  // abort the youngest transaction
        std::cout << "Deadlock detected. Aborting youngest transaction: T" << victim << "\n";
        tm.abort(victim);
        std::cout << "After abort, T" << t1 << " retries X(B): "
                  << name(lm.acquire(t1, B, LockMode::Exclusive)) << "\n";
    } else {
        std::cout << "ERROR: expected a deadlock\n";
        return 1;
    }
    return 0;
}
