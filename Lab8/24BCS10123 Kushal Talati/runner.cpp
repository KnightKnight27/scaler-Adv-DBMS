// Lab 8 — driver for the MVCC + 2PL transaction store
// 24BCS10123  Kushal Talati
//
// Five scenarios, each self-checked:
//   1. MVCC snapshot isolation — a reader keeps its snapshot after a concurrent
//      writer commits.
//   2. Strict 2PL — a second writer on the same key is BLOCKED until the holder
//      releases.
//   3. Deadlock — two writers lock in opposite order; the cycle rolls back the
//      youngest transaction.
//   4. First-committer-wins — a concurrent overwrite is rejected at commit with
//      CONFLICT.
//   5. vacuum() — dead revisions below the oldest snapshot are reclaimed.

#include "txn_store.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void section(const std::string& s) { std::cout << "\n>>> " << s << "\n"; }

void expect(bool ok, const std::string& what) {
    std::cout << (ok ? "  [ok] " : "  [XX] ") << what << "\n";
    if (!ok) std::exit(1);
}

// Read helper: returns the value, or "<none>" when the key is not visible.
std::string peek(kt::TxnStore& s, kt::TxnId tx, const std::string& key) {
    std::string v;
    return s.get(tx, key, v) == kt::Outcome::Ok ? v : std::string("<none>");
}

}  // namespace

int main() {
    kt::TxnStore store;

    // ----------------------------------------------------------------------
    section("1) MVCC snapshot isolation — a reader ignores later commits");
    // ----------------------------------------------------------------------
    {
        const kt::TxnId seed = store.start();
        store.put(seed, "x", "100");
        expect(store.commit(seed) == kt::Outcome::Ok, "seed x=100 committed");

        const kt::TxnId t1 = store.start();            // snapshot sees x=100
        std::cout << "  T" << t1 << " reads x = " << peek(store, t1, "x") << "\n";

        const kt::TxnId t2 = store.start();
        store.put(t2, "x", "200");
        expect(store.commit(t2) == kt::Outcome::Ok, "T2 commits x=200");

        const std::string again = peek(store, t1, "x");
        std::cout << "  T" << t1 << " re-reads x = " << again << " (its own snapshot)\n";
        expect(again == "100", "T1 still sees x=100 after T2 committed x=200");

        const kt::TxnId t3 = store.start();            // fresh snapshot
        expect(peek(store, t3, "x") == "200", "a transaction started later sees x=200");

        store.commit(t1);
        store.commit(t3);
    }

    // ----------------------------------------------------------------------
    section("2) Strict 2PL — the exclusive write lock blocks a second writer");
    // ----------------------------------------------------------------------
    {
        const kt::TxnId t1 = store.start();
        const kt::TxnId t2 = store.start();
        expect(store.put(t1, "y", "1") == kt::Outcome::Ok,      "T1 takes the X-lock on y");
        expect(store.put(t2, "y", "2") == kt::Outcome::Blocked, "T2 write y -> BLOCKED");

        store.rollback(t1);                            // releases the lock, publishes nothing
        expect(store.phase(t1) == kt::Phase::Aborted,  "T1 rolls back");
        expect(store.put(t2, "y", "2") == kt::Outcome::Ok, "T2 retries write y -> OK");
        expect(store.commit(t2) == kt::Outcome::Ok,    "T2 commits");

        const kt::TxnId t3 = store.start();
        expect(peek(store, t3, "y") == "2", "final y = 2");
        store.commit(t3);
    }

    // ----------------------------------------------------------------------
    section("3) Deadlock detection — the youngest transaction is the victim");
    // ----------------------------------------------------------------------
    {
        const kt::TxnId t1 = store.start();
        const kt::TxnId t2 = store.start();
        expect(store.put(t1, "a", "A1") == kt::Outcome::Ok, "T1 locks a");
        expect(store.put(t2, "b", "B1") == kt::Outcome::Ok, "T2 locks b");

        expect(store.put(t1, "b", "B2") == kt::Outcome::Blocked, "T1 wants b -> waits for T2");
        const kt::Outcome o = store.put(t2, "a", "A2");          // closes the cycle T1->T2->T1
        std::cout << "  T2 wants a -> " << kt::name_of(o)
                  << "; victim = T" << store.last_victim() << "\n";
        expect(o == kt::Outcome::RolledBack, "T2 (younger) is rolled back to break the cycle");
        expect(store.last_victim() == t2,    "victim is the higher-id (younger) txn");
        expect(store.phase(t2) == kt::Phase::Aborted, "T2 is Aborted");

        expect(store.put(t1, "b", "B2") == kt::Outcome::Ok, "T1 acquires b after T2 died");
        expect(store.commit(t1) == kt::Outcome::Ok,         "T1 commits a=A1, b=B2");

        const kt::TxnId chk = store.start();
        expect(peek(store, chk, "a") == "A1" && peek(store, chk, "b") == "B2",
               "a=A1, b=B2 persisted");
        store.commit(chk);
    }

    // ----------------------------------------------------------------------
    section("4) First-committer-wins — a concurrent overwrite fails at commit");
    // ----------------------------------------------------------------------
    {
        const kt::TxnId seed = store.start();
        store.put(seed, "z", "0");
        store.commit(seed);

        const kt::TxnId t1 = store.start();            // both snapshot z=0 before writing
        const kt::TxnId t2 = store.start();

        expect(store.put(t1, "z", "10") == kt::Outcome::Ok,      "T1 writes z=10 (takes lock)");
        expect(store.put(t2, "z", "20") == kt::Outcome::Blocked, "T2 write z blocks on T1");

        expect(store.commit(t1) == kt::Outcome::Ok, "T1 commits z=10");

        expect(store.put(t2, "z", "20") == kt::Outcome::Ok, "T2 takes the lock after T1 released");
        const kt::Outcome o = store.commit(t2);
        std::cout << "  T2 commit -> " << kt::name_of(o) << "\n";
        expect(o == kt::Outcome::Conflict,
               "T2 rejected: it snapshotted z=0 but z changed underneath it");

        const kt::TxnId chk = store.start();
        expect(peek(store, chk, "z") == "10", "z = 10 (first committer won)");
        store.commit(chk);
    }

    // ----------------------------------------------------------------------
    section("5) vacuum() — reclaim dead revisions below the oldest snapshot");
    // ----------------------------------------------------------------------
    {
        const std::size_t before  = store.revision_count();
        const std::size_t dropped = store.vacuum();    // no active txn holds an old snapshot
        const std::size_t after   = store.revision_count();
        std::cout << "  revisions: " << before << " -> " << after
                  << "  (dropped " << dropped << ")\n";
        expect(dropped > 0, "at least one dead revision is reclaimed");
        expect(before - dropped == after, "revision count is consistent after vacuum");

        const kt::TxnId chk = store.start();
        expect(peek(store, chk, "x") == "200", "x=200 survives vacuum");
        expect(peek(store, chk, "z") == "10",  "z=10 survives vacuum");
        store.commit(chk);
    }

    std::cout << "\nAll transaction-store checks passed.\n";
    return 0;
}
