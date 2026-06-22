// deadlock_demo.cpp
//
// Demonstrates the 2PL deadlock detection required by the capstone guidelines
// ("Students should demonstrate ... Deadlock scenarios").
//
// Two transactions acquire two resources in OPPOSITE order, forming a cycle:
//
//     T1: lock(A)  then  lock(B)
//     T2: lock(B)  then  lock(A)
//
// Once both hold their first lock and request the second, the LockManager's
// waits-for graph contains the cycle  T1 → T2 → T1.  The second transaction to
// request its conflicting lock detects the cycle and is thrown a
// DeadlockException — it becomes the "victim", releases its locks, and the
// other transaction proceeds to completion.
//
// Build: produced by CMakeLists.txt as the `deadlock_demo` target.
// Run:   ./build/deadlock_demo

#include "../src/transaction.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace ch = std::chrono;

int main() {
    LockManager lm;

    // A tiny barrier so both transactions hold their FIRST lock before either
    // requests its SECOND — this is what guarantees the cyclic wait.
    std::atomic<int> ready{0};
    auto wait_for_both = [&]() {
        ready.fetch_add(1);
        while (ready.load() < 2) std::this_thread::yield();
    };

    std::atomic<bool> t1_victim{false}, t2_victim{false};
    std::atomic<bool> t1_done{false},   t2_done{false};

    std::cout << "\n=== MiniDB Deadlock Detection Demo (2PL) ===\n\n";
    std::cout << "T1 wants: A then B    |    T2 wants: B then A\n\n";

    // Transaction 1: lock A, then B
    std::thread t1([&]() {
        const TxnId id = 1;
        try {
            lm.lock(id, "A", LockMode::EXCLUSIVE);
            std::cout << "[T1] acquired EXCLUSIVE lock on A\n";
            wait_for_both();
            std::cout << "[T1] requesting EXCLUSIVE lock on B ...\n";
            lm.lock(id, "B", LockMode::EXCLUSIVE);   // may block / may be victim
            std::cout << "[T1] acquired EXCLUSIVE lock on B  -> T1 COMMITS\n";
            lm.unlock_all(id);
            t1_done = true;
        } catch (const DeadlockException& e) {
            std::cout << "[T1] DEADLOCK detected — T1 chosen as victim, aborting\n";
            lm.unlock_all(id);
            t1_victim = true;
        }
    });

    // Transaction 2: lock B, then A
    std::thread t2([&]() {
        const TxnId id = 2;
        try {
            lm.lock(id, "B", LockMode::EXCLUSIVE);
            std::cout << "[T2] acquired EXCLUSIVE lock on B\n";
            wait_for_both();
            std::cout << "[T2] requesting EXCLUSIVE lock on A ...\n";
            lm.lock(id, "A", LockMode::EXCLUSIVE);   // may block / may be victim
            std::cout << "[T2] acquired EXCLUSIVE lock on A  -> T2 COMMITS\n";
            lm.unlock_all(id);
            t2_done = true;
        } catch (const DeadlockException& e) {
            std::cout << "[T2] DEADLOCK detected — T2 chosen as victim, aborting\n";
            lm.unlock_all(id);
            t2_victim = true;
        }
    });

    t1.join();
    t2.join();

    std::cout << "\n--- Outcome ---\n";
    int victims    = (t1_victim ? 1 : 0) + (t2_victim ? 1 : 0);
    int committed  = (t1_done   ? 1 : 0) + (t2_done   ? 1 : 0);
    std::cout << "Victims (aborted): " << victims
              << "   Committed: " << committed << "\n";

    if (victims == 1 && committed == 1) {
        std::cout << "PASS: exactly one transaction was aborted to break the cycle;\n"
                     "      the other ran to completion. Deadlock handled correctly.\n\n";
        return 0;
    }
    std::cout << "UNEXPECTED: the demo did not produce a clean one-victim outcome.\n\n";
    return 1;
}
