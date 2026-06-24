// Deterministic, single-threaded simulation of the MV2PL transaction manager.
// Each scenario uses a fresh TxnManager (clock restarts at 0), and the
// interleaving is explicit, so the output below is fully reproducible.
#include "txn_manager.h"
#include <iostream>
#include <string>
#include <cassert>

static std::string show(const std::optional<long long>& v) {
    return v ? std::to_string(*v) : std::string("<absent>");
}

static void scenario1_mvcc_visibility() {
    std::cout << "========== Scenario 1: MVCC snapshot visibility ==========\n";
    TxnManager db;

    int t1 = db.begin();
    std::cout << "T1=" << t1 << " writes k=10\n";
    db.write(t1, "k", 10);

    int t2 = db.begin();                       // snapshot taken before T1 commits
    auto r = db.read(t2, "k");
    std::cout << "T2=" << t2 << " reads k -> " << show(r) << " (expected <absent>)\n";
    assert(!r);

    db.commit(t1);
    std::cout << "T1 commits\n";

    r = db.read(t2, "k");                      // still invisible: snapshot predates commit
    std::cout << "T2 reads k -> " << show(r)
              << " (expected <absent>, snapshot predates commit)\n";
    assert(!r);

    int t3 = db.begin();                       // fresh snapshot after the commit
    r = db.read(t3, "k");
    std::cout << "T3=" << t3 << " reads k -> " << show(r) << " (expected 10)\n";
    assert(r && *r == 10);

    std::cout << "PASS: MVCC visibility correct.\n\n";
}

static void scenario2_write_write() {
    std::cout << "========== Scenario 2: Write-write conflict under Strict 2PL ==========\n";
    TxnManager db;

    int t1 = db.begin();
    auto a = db.write(t1, "k", 1);
    std::cout << "T1=" << t1 << " write k=1 -> OK (acquires X on k)\n";
    assert(a == LockResult::Granted);

    int t2 = db.begin();
    auto b = db.write(t2, "k", 2);
    std::cout << "T2=" << t2 << " write k=2 -> BLOCKED (X held by T1 => must wait)\n";
    assert(b == LockResult::Blocked);

    db.commit(t1);
    std::cout << "T1 commits (releases X)\n";

    auto c = db.write(t2, "k", 2);
    std::cout << "T2 retries write k=2 -> OK\n";
    assert(c == LockResult::Granted);
    db.commit(t2);

    std::cout << "PASS: write-write conflict correctly serialized.\n\n";
}

static void scenario3_deadlock() {
    std::cout << "========== Scenario 3: Deadlock (T1:A->B, T2:B->A) ==========\n";
    TxnManager db;

    int t1 = db.begin();
    int t2 = db.begin();

    assert(db.write(t1, "A", 100) == LockResult::Granted);
    std::cout << "T1=" << t1 << " write A=100 -> OK (X on A)\n";

    assert(db.write(t2, "B", 200) == LockResult::Granted);
    std::cout << "T2=" << t2 << " write B=200 -> OK (X on B)\n";

    auto r1 = db.write(t1, "B", 101);          // T1 waits on T2 (T1->T2): benign
    std::cout << "T1 write B=101 -> BLOCKED (waits on T2; wait-for: T1->T2)\n";
    assert(r1 == LockResult::Blocked);

    auto r2 = db.write(t2, "A", 201);          // closes the cycle T2->T1->T2
    std::cout << "T2 write A=201 -> ABORTED (closes cycle => deadlock => T2 aborted)\n";
    assert(r2 == LockResult::Deadlock);        // do_write auto-aborts the victim

    auto r3 = db.write(t1, "B", 101);          // survivor proceeds
    std::cout << "T1 retries write B=101 -> OK\n";
    assert(r3 == LockResult::Granted);
    db.commit(t1);

    int t3 = db.begin();
    auto A = db.read(t3, "A");
    auto B = db.read(t3, "B");
    std::cout << "T3 reads A=" << show(A) << " B=" << show(B)
              << " (expected A=100, B=101)\n";
    assert(A && *A == 100 && B && *B == 101);

    std::cout << "PASS: deadlock detected, one victim aborted, data consistent.\n\n";
}

static void scenario4_abort_rollback() {
    std::cout << "========== Scenario 4: Abort rollback ==========\n";
    TxnManager db;

    int setup = db.begin();                    // seed committed x=7
    db.write(setup, "x", 7);
    db.commit(setup);

    int t2 = db.begin();
    std::cout << "T2=" << t2 << " write x=999 then ABORT\n";
    db.write(t2, "x", 999);
    db.abort(t2);

    int t3 = db.begin();
    auto r = db.read(t3, "x");
    std::cout << "T3=" << t3 << " reads x -> " << show(r)
              << " (expected 7, the aborted write is invisible)\n";
    assert(r && *r == 7);

    std::cout << "PASS: aborted write rolled back cleanly.\n\n";
}

static void scenario5_tombstone() {
    std::cout << "========== Scenario 5: Delete tombstone (MVCC) ==========\n";
    TxnManager db;

    int setup = db.begin();                    // seed committed d=5
    db.write(setup, "d", 5);
    db.commit(setup);

    int old_snap = db.begin();                 // snapshot taken before the delete

    int del = db.begin();                      // delete d and commit
    db.remove(del, "d");
    db.commit(del);

    auto ro = db.read(old_snap, "d");          // old snapshot still sees d=5
    std::cout << "Old snapshot reads d -> " << show(ro) << " (expected 5)\n";
    assert(ro && *ro == 5);

    int new_snap = db.begin();                 // fresh snapshot sees the tombstone
    auto rn = db.read(new_snap, "d");
    std::cout << "New snapshot reads d -> " << show(rn)
              << " (expected <absent>, tombstone)\n";
    assert(!rn);

    std::cout << "PASS: tombstone visibility correct.\n\n";
}

int main() {
    scenario1_mvcc_visibility();
    scenario2_write_write();
    scenario3_deadlock();
    scenario4_abort_rollback();
    scenario5_tombstone();
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
