// main.cc — ADBMS Lab 8 demo / 24BCS10115 Gauri Shukla
//
// Drives the transaction manager through five scenarios, each asserting the
// expected outcome:
//   1. MVCC snapshot isolation — a reader keeps seeing its snapshot after a
//      concurrent writer commits.
//   2. Strict 2PL — a second writer on the same key gets LOCK_WAIT until the
//      holder commits.
//   3. Deadlock — two writers lock in opposite order; the cycle aborts the
//      youngest transaction.
//   4. First-updater-wins — a concurrent overwrite is rejected at commit with
//      SERIALIZATION_FAILURE.
//   5. gc() — dead versions older than the oldest snapshot are pruned.

#include "txn_manager.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace mvcc;

namespace {

void banner(const std::string& s) { std::cout << "\n=== " << s << " ===\n"; }

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [pass] " : "  [FAIL] ") << what << "\n";
    if (!cond) std::exit(1);
}

// Read helper that prints and returns the value (or "<none>").
std::string rd(TxnManager& tm, TxId tx, const std::string& key) {
    std::string v;
    Status s = tm.read(tx, key, v);
    return s == Status::Ok ? v : std::string("<none>");
}

}  // namespace

int main() {
    TxnManager tm;

    // --------------------------------------------------------------------
    banner("1) MVCC snapshot isolation — readers don't see later commits");
    // --------------------------------------------------------------------
    {
        TxId setup = tm.begin();
        tm.write(setup, "x", "100");
        check(tm.commit(setup) == Status::Ok, "seed x=100 committed");

        TxId t1 = tm.begin();                  // snapshot taken now (sees x=100)
        std::cout << "  T" << t1 << " reads x = " << rd(tm, t1, "x") << "\n";

        TxId t2 = tm.begin();
        tm.write(t2, "x", "200");
        check(tm.commit(t2) == Status::Ok, "T2 commits x=200");

        std::string v = rd(tm, t1, "x");
        std::cout << "  T" << t1 << " re-reads x = " << v << " (still its snapshot)\n";
        check(v == "100", "T1 still sees x=100 after T2 committed x=200");

        TxId t3 = tm.begin();                  // fresh snapshot
        check(rd(tm, t3, "x") == "200", "a new transaction sees x=200");

        tm.commit(t1);
        tm.commit(t3);
    }

    // --------------------------------------------------------------------
    banner("2) Strict 2PL — exclusive write lock blocks a second writer");
    // --------------------------------------------------------------------
    {
        TxId t1 = tm.begin();
        TxId t2 = tm.begin();
        check(tm.write(t1, "y", "1") == Status::Ok, "T1 takes X-lock on y");
        check(tm.write(t2, "y", "2") == Status::LockWait, "T2 write y -> LOCK_WAIT");

        // T1 rolls back, so it never publishes a version — T2 can then proceed
        // and commit cleanly (no write-write conflict to fail at commit).
        tm.abort(t1);
        check(tm.state(t1) == TxState::Aborted, "T1 aborts (releases lock)");
        check(tm.write(t2, "y", "2") == Status::Ok, "T2 retries write y -> OK");
        check(tm.commit(t2) == Status::Ok, "T2 commits");

        TxId t3 = tm.begin();
        check(rd(tm, t3, "y") == "2", "final y = 2");
        tm.commit(t3);
    }

    // --------------------------------------------------------------------
    banner("3) Deadlock detection — youngest transaction is the victim");
    // --------------------------------------------------------------------
    {
        TxId t1 = tm.begin();
        TxId t2 = tm.begin();
        check(tm.write(t1, "a", "A1") == Status::Ok, "T1 locks a");
        check(tm.write(t2, "b", "B1") == Status::Ok, "T2 locks b");

        check(tm.write(t1, "b", "B2") == Status::LockWait, "T1 wants b -> waits for T2");
        Status s = tm.write(t2, "a", "A2");    // closes the cycle T1->T2->T1
        std::cout << "  T2 wants a -> " << to_string(s)
                  << "; victim = T" << tm.last_victim() << "\n";
        check(s == Status::Aborted, "T2 (younger) is aborted to break the cycle");
        check(tm.last_victim() == t2, "victim is the higher-id (younger) txn");
        check(tm.state(t2) == TxState::Aborted, "T2 state is Aborted");

        check(tm.write(t1, "b", "B2") == Status::Ok, "T1 now acquires b after T2 died");
        check(tm.commit(t1) == Status::Ok, "T1 commits a=A1, b=B2");

        TxId chk = tm.begin();
        check(rd(tm, chk, "a") == "A1" && rd(tm, chk, "b") == "B2", "a=A1, b=B2 persisted");
        tm.commit(chk);
    }

    // --------------------------------------------------------------------
    banner("4) First-updater-wins — concurrent overwrite fails at commit");
    // --------------------------------------------------------------------
    {
        TxId seed = tm.begin();
        tm.write(seed, "z", "0");
        tm.commit(seed);

        TxId t1 = tm.begin();                  // both snapshot z=0 before either writes
        TxId t2 = tm.begin();

        check(tm.write(t1, "z", "10") == Status::Ok, "T1 writes z=10 (takes lock)");
        check(tm.write(t2, "z", "20") == Status::LockWait, "T2 write z blocks on T1");

        check(tm.commit(t1) == Status::Ok, "T1 commits z=10");

        check(tm.write(t2, "z", "20") == Status::Ok, "T2 acquires lock after T1 released");
        Status s = tm.commit(t2);
        std::cout << "  T2 commit -> " << to_string(s) << "\n";
        check(s == Status::SerializationFailure,
              "T2 rejected: it read z@snapshot0 but z changed under it");

        TxId chk = tm.begin();
        check(rd(tm, chk, "z") == "10", "z = 10 (first updater won)");
        tm.commit(chk);
    }

    // --------------------------------------------------------------------
    banner("5) gc() — prune dead versions below the oldest live snapshot");
    // --------------------------------------------------------------------
    {
        std::size_t before = tm.version_count();
        std::size_t pruned = tm.gc();          // no active txns hold old snapshots now
        std::size_t after  = tm.version_count();
        std::cout << "  versions: " << before << " -> " << after
                  << "  (pruned " << pruned << ")\n";
        check(pruned > 0, "at least one dead version was reclaimed");
        check(before - pruned == after, "version count is consistent after gc");

        // Latest values must survive the vacuum.
        TxId chk = tm.begin();
        check(rd(tm, chk, "x") == "200", "x=200 survives gc");
        check(rd(tm, chk, "z") == "10",  "z=10 survives gc");
        tm.commit(chk);
    }

    std::cout << "\nAll transaction-manager checks passed.\n";
    return 0;
}
