// =============================================================================
// tests/transaction/transaction_test.cpp
// -----------------------------------------------------------------------------
// Exercises the strict-2PL lock manager and the MVCC transaction manager.
//
//   Part 1 — basic grants / release: shared locks coexist; after the shared
//            holders release, an exclusive lock is granted.
//
//   Part 2 — a REAL deadlock. Two threads each hold an X lock on one record
//            and then reach for the other's record, forming a wait-for cycle
//            A -> B -> A. The lock manager must detect the cycle and return
//            DEADLOCK to exactly one thread (the victim); the victim releases
//            its lock, waking the other, which then succeeds. Net: one
//            DEADLOCK and one OK. This replaces the v1 stub that returned
//            DEADLOCK on any conflict without ever blocking.
//
//   Part 3 — MVCC write-write conflict: the first-updater-wins rule aborts a
//            txn that writes a rid already written by a concurrently-committed
//            txn.
// =============================================================================
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

#include "transaction/lock_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;

int main() {
    // ----- Part 1: basic grants and a clean exclusive upgrade -----
    transaction::LockManager lm;
    assert(lm.acquireShared(1, 42) == Status::OK);
    assert(lm.acquireShared(2, 42) == Status::OK);
    // With real blocking, an X request while S locks are held would PARK the
    // caller. We don't issue it single-threaded (it would hang); we release
    // the S holders first, then take X cleanly.
    lm.releaseAll(1);
    lm.releaseAll(2);
    assert(lm.acquireExclusive(3, 42) == Status::OK);
    lm.releaseAll(3);

    // ----- Part 2: a real deadlock via a wait-for cycle -----
    constexpr RecordId ridA = 100;
    constexpr RecordId ridB = 200;
    std::atomic<int>  deadlocks{0};
    std::atomic<int>  oks{0};
    std::atomic<bool> readyA{false}, readyB{false}, go{false};

    auto worker = [&](TransactionId txn, RecordId first, RecordId second,
                      std::atomic<bool>& ready) {
        // Step 1: grab our own record exclusively. Always succeeds (no one
        // else holds it yet).
        assert(lm.acquireExclusive(txn, first) == Status::OK);
        ready.store(true);
        // Barrier: wait until BOTH threads hold their first lock, then race
        // for the other's record simultaneously.
        while (!go.load()) { /* spin */ }
        // Step 2: reach for the other thread's record. One of us will close
        // the wait-for cycle and be told DEADLOCK; the other will block,
        // then succeed once the victim releases.
        Status s = lm.acquireExclusive(txn, second);
        if (s == Status::DEADLOCK) {
            ++deadlocks;
            lm.releaseAll(txn);   // victim: break the cycle
        } else if (s == Status::OK) {
            ++oks;
            lm.releaseAll(txn);
        }
    };

    std::thread tA(worker, 10, ridA, ridB, std::ref(readyA));
    std::thread tB(worker, 11, ridB, ridA, std::ref(readyB));
    while (!(readyA.load() && readyB.load())) { /* wait for both first locks */ }
    go.store(true);
    tA.join();
    tB.join();

    // Exactly one victim, exactly one survivor.
    assert(deadlocks.load() == 1);
    assert(oks.load() == 1);

    // ----- Part 3: MVCC write-write conflict (first-updater-wins) -----
    transaction::TransactionManager tm;
    TransactionId t1 = tm.begin();
    TransactionId t2 = tm.begin();
    tm.recordWrite(t1, 7);
    assert(tm.commit(t1) == Status::OK);
    tm.recordWrite(t2, 7);
    assert(tm.commit(t2) == Status::TXN_CONFLICT);
    assert(tm.abort(t2) == Status::OK);

    std::printf("[OK] 2PL blocking, real deadlock detection, MVCC conflict\n");
    return 0;
}