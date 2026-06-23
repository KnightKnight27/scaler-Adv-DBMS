// main.cpp
// -----------------------------------------------------------------------------
// Deterministic, single-threaded demonstration + self-tests for the
// MVCC + Strict 2PL + deadlock-detection transaction manager.
//
// All concurrency is simulated by explicitly interleaving operations of several
// transactions, so the deadlock scenario and every assertion are reproducible.
// -----------------------------------------------------------------------------
#include <cassert>
#include <iostream>
#include <string>

#include "txn_manager.h"

using namespace lab8;

namespace {

void banner(const std::string& title) {
    std::cout << "\n========== " << title << " ==========\n";
}

const char* op_name(OpStatus s) {
    switch (s) {
        case OpStatus::Ok:      return "OK";
        case OpStatus::Blocked: return "BLOCKED";
        case OpStatus::Aborted: return "ABORTED";
    }
    return "?";
}

}  // namespace

// ---------------------------------------------------------------------------
// Scenario 1: MVCC snapshot visibility.
//   T1 writes k=10 and commits. T2 (snapshot BEFORE T1 commits) must NOT see
//   it. T3 (snapshot AFTER T1 commits) must see 10.
// ---------------------------------------------------------------------------
void test_mvcc_visibility() {
    banner("Scenario 1: MVCC snapshot visibility");
    TxnManager db;

    TxnId t1 = db.begin();
    TxnId t2 = db.begin();              // snapshot taken BEFORE T1 commits

    std::cout << "T1=" << t1 << " writes k=10\n";
    assert(db.write(t1, "k", 10) == OpStatus::Ok);

    // Before T1 commits, even a brand-new reader other than T1 sees nothing.
    ReadResult r2_pre = db.read(t2, "k");
    std::cout << "T2=" << t2 << " reads k -> "
              << (r2_pre.found ? std::to_string(*r2_pre.value) : "<absent>")
              << " (expected <absent>)\n";
    assert(!r2_pre.found);

    std::cout << "T1 commits\n";
    db.commit(t1);

    // T2's snapshot predates T1's commit -> still must NOT see k=10.
    ReadResult r2 = db.read(t2, "k");
    std::cout << "T2 reads k -> "
              << (r2.found ? std::to_string(*r2.value) : "<absent>")
              << " (expected <absent>, snapshot predates commit)\n";
    assert(!r2.found);

    // T3 begins AFTER T1 committed -> sees k=10.
    TxnId t3 = db.begin();
    ReadResult r3 = db.read(t3, "k");
    std::cout << "T3=" << t3 << " reads k -> "
              << (r3.found ? std::to_string(*r3.value) : "<absent>")
              << " (expected 10)\n";
    assert(r3.found && *r3.value == 10);

    db.commit(t2);
    db.commit(t3);
    std::cout << "PASS: MVCC visibility correct.\n";
}

// ---------------------------------------------------------------------------
// Scenario 2: Write-write conflict under Strict 2PL.
//   T1 takes X on k; T2's write to k must BLOCK (wait), not overwrite.
// ---------------------------------------------------------------------------
void test_ww_conflict() {
    banner("Scenario 2: Write-write conflict under Strict 2PL");
    TxnManager db;

    TxnId t1 = db.begin();
    TxnId t2 = db.begin();

    std::cout << "T1=" << t1 << " write k=1 -> ";
    OpStatus s1 = db.write(t1, "k", 1);
    std::cout << op_name(s1) << " (acquires X on k)\n";
    assert(s1 == OpStatus::Ok);

    std::cout << "T2=" << t2 << " write k=2 -> ";
    OpStatus s2 = db.write(t2, "k", 2);
    std::cout << op_name(s2) << " (X held by T1 => must wait)\n";
    assert(s2 == OpStatus::Blocked);

    // T1 still holds the X lock (Strict 2PL: not released before commit).
    assert(db.locks().holds(t1, "k", LockMode::X));

    std::cout << "T1 commits (releases X)\n";
    db.commit(t1);
    assert(!db.locks().holds(t1, "k", LockMode::X));

    // Now T2 can proceed.
    std::cout << "T2 retries write k=2 -> ";
    OpStatus s2b = db.write(t2, "k", 2);
    std::cout << op_name(s2b) << "\n";
    assert(s2b == OpStatus::Ok);
    db.commit(t2);
    std::cout << "PASS: write-write conflict correctly serialized.\n";
}

// ---------------------------------------------------------------------------
// Scenario 3: Deadlock detection + victim abort.
//   T1 locks A then wants B; T2 locks B then wants A => cycle. Detector aborts
//   exactly one victim; the survivor commits and data stays consistent.
// ---------------------------------------------------------------------------
void test_deadlock() {
    banner("Scenario 3: Deadlock (T1:A->B, T2:B->A)");
    TxnManager db;

    TxnId t1 = db.begin();
    TxnId t2 = db.begin();

    std::cout << "T1=" << t1 << " write A=100 -> ";
    assert(db.write(t1, "A", 100) == OpStatus::Ok);
    std::cout << "OK (X on A)\n";

    std::cout << "T2=" << t2 << " write B=200 -> ";
    assert(db.write(t2, "B", 200) == OpStatus::Ok);
    std::cout << "OK (X on B)\n";

    // T1 wants B (held by T2) -> blocks, edge T1->T2. No cycle yet.
    std::cout << "T1 write B=101 -> ";
    OpStatus s1b = db.write(t1, "B", 101);
    std::cout << op_name(s1b) << " (waits on T2; wait-for: T1->T2)\n";
    assert(s1b == OpStatus::Blocked);

    // T2 wants A (held by T1) -> edge T2->T1 closes the cycle T1<->T2.
    std::cout << "T2 write A=201 -> ";
    OpStatus s2a = db.write(t2, "A", 201);
    std::cout << op_name(s2a) << " (closes cycle => deadlock => T2 aborted)\n";
    assert(s2a == OpStatus::Aborted);

    // Exactly one victim aborted.
    assert(db.state(t2) == TxnState::Aborted);
    assert(db.state(t1) == TxnState::Active);

    // Survivor T1 can now grab B (T2 released its locks on abort) and commit.
    std::cout << "T1 retries write B=101 -> ";
    OpStatus s1b2 = db.write(t1, "B", 101);
    std::cout << op_name(s1b2) << "\n";
    assert(s1b2 == OpStatus::Ok);
    db.commit(t1);
    assert(db.state(t1) == TxnState::Committed);

    // Consistency: a fresh reader sees T1's committed values, none of T2's.
    TxnId t3 = db.begin();
    ReadResult a = db.read(t3, "A");
    ReadResult b = db.read(t3, "B");
    std::cout << "T3 reads A=" << *a.value << " B=" << *b.value
              << " (expected A=100, B=101)\n";
    assert(a.found && *a.value == 100);
    assert(b.found && *b.value == 101);
    db.commit(t3);
    std::cout << "PASS: deadlock detected, one victim aborted, data consistent.\n";
}

// ---------------------------------------------------------------------------
// Scenario 4: Abort rollback. An aborted txn's writes are invisible afterward.
// ---------------------------------------------------------------------------
void test_abort_rollback() {
    banner("Scenario 4: Abort rollback");
    TxnManager db;

    TxnId t1 = db.begin();
    assert(db.write(t1, "x", 7) == OpStatus::Ok);
    db.commit(t1);                          // committed baseline x=7

    TxnId t2 = db.begin();
    std::cout << "T2=" << t2 << " write x=999 then ABORT\n";
    assert(db.write(t2, "x", 999) == OpStatus::Ok);
    db.abort(t2);
    assert(db.state(t2) == TxnState::Aborted);

    // A reader after the abort must see the old committed value, not 999.
    TxnId t3 = db.begin();
    ReadResult r = db.read(t3, "x");
    std::cout << "T3=" << t3 << " reads x -> " << *r.value
              << " (expected 7, the aborted write is invisible)\n";
    assert(r.found && *r.value == 7);
    db.commit(t3);
    std::cout << "PASS: aborted write rolled back cleanly.\n";
}

// ---------------------------------------------------------------------------
// Bonus: tombstone delete visibility.
// ---------------------------------------------------------------------------
void test_delete_tombstone() {
    banner("Scenario 5: Delete tombstone (MVCC)");
    TxnManager db;

    TxnId t1 = db.begin();
    assert(db.write(t1, "d", 5) == OpStatus::Ok);
    db.commit(t1);

    TxnId told = db.begin();                // snapshot sees d=5
    TxnId t2   = db.begin();
    assert(db.remove(t2, "d") == OpStatus::Ok);
    db.commit(t2);

    ReadResult rold = db.read(told, "d");
    std::cout << "Old snapshot reads d -> "
              << (rold.found ? std::to_string(*rold.value) : "<absent>")
              << " (expected 5)\n";
    assert(rold.found && *rold.value == 5);

    TxnId tnew = db.begin();                // snapshot after delete
    ReadResult rnew = db.read(tnew, "d");
    std::cout << "New snapshot reads d -> "
              << (rnew.found ? std::to_string(*rnew.value) : "<absent>")
              << " (expected <absent>, tombstone)\n";
    assert(!rnew.found);

    db.commit(told);
    db.commit(tnew);
    std::cout << "PASS: tombstone visibility correct.\n";
}

int main() {
    test_mvcc_visibility();
    test_ww_conflict();
    test_deadlock();
    test_abort_rollback();
    test_delete_tombstone();

    std::cout << "\nALL TESTS PASSED\n";
    return 0;
}
