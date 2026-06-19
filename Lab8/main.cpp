// main.cpp — ADBMS Lab 8 / 24BCS10193 Anushka Jain
//
// Validates the transaction manager through six bank-account scenarios:
//
//  1. MVCC Snapshot Isolation  — a long-running reader keeps seeing the
//     value that existed when it started, even after a concurrent writer
//     commits a new value.
//
//  2. Tombstone Visibility     — a deleted key is invisible to snapshots
//     taken after the delete commits, but still readable by older ones.
//
//  3. Strict 2PL               — a second writer trying to update a
//     locked key receives LOCK_WAIT, then succeeds after the lock is
//     released.
//
//  4. Deadlock Detection       — two transfers (A→B and B→A) form a
//     waits-for cycle; the manager aborts the younger transaction.
//
//  5. First-Updater-Wins       — only the first concurrent writer to
//     commit survives; the second gets SERIALIZATION_FAILURE.
//
//  6. Garbage Collection       — after all long-running readers finish,
//     gc() reclaims dead versions while keeping live ones intact.
//
// Every scenario is followed by check_invariants() to verify structural
// consistency of locks and version chains.

#include "txn_manager.hpp"
#include <cassert>
#include <iostream>
#include <string>

using adbms::txn::Manager;
using adbms::txn::Result;
using adbms::txn::State;
using adbms::txn::txn_id_t;

namespace {

void banner(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

void verify(bool condition, const std::string& label) {
    std::cout << (condition ? "  [pass] " : "  [FAIL] ") << label << "\n";
    if (!condition) std::exit(1);
}

void assert_invariants(const Manager& m, const std::string& ctx) {
    std::string err = m.check_invariants();
    if (!err.empty()) {
        std::cerr << "INVARIANT VIOLATION after " << ctx << ": " << err << "\n";
        std::exit(1);
    }
}

// Convenience wrapper: reads key and returns its value, or "<none>" when
// the key is absent or has been deleted.
std::string safe_read(Manager& m, txn_id_t tx, const std::string& key) {
    std::string val;
    return m.read(tx, key, val) == Result::Ok ? val : "<none>";
}

}  // namespace

int main() {
    Manager bank;

    // ─────────────────────────────────────────────────────────────────────────
    banner("0) Seed: alice=1000, bob=500, carol=750");
    // ─────────────────────────────────────────────────────────────────────────
    {
        txn_id_t seed = bank.begin();
        bank.write(seed, "alice", "1000");
        bank.write(seed, "bob",   "500");
        bank.write(seed, "carol", "750");
        verify(bank.commit(seed) == Result::Ok, "seed transaction committed");
        assert_invariants(bank, "seed");
    }

    // ─────────────────────────────────────────────────────────────────────────
    banner("1) MVCC Snapshot Isolation");
    // ─────────────────────────────────────────────────────────────────────────
    // A reader started before a write should keep seeing the old value.
    {
        txn_id_t old_reader = bank.begin();
        verify(safe_read(bank, old_reader, "alice") == "1000",
               "old_reader sees alice=1000 before write");

        txn_id_t updater = bank.begin();
        verify(bank.write(updater, "alice", "1500") == Result::Ok,
               "updater acquires lock on alice");
        verify(bank.commit(updater) == Result::Ok,
               "updater commits alice=1500");

        // old_reader's snapshot predates the commit, so it still reads 1000.
        verify(safe_read(bank, old_reader, "alice") == "1000",
               "old_reader still sees alice=1000 (snapshot isolation)");

        // A brand-new reader must see the committed value 1500.
        txn_id_t new_reader = bank.begin();
        verify(safe_read(bank, new_reader, "alice") == "1500",
               "new_reader sees alice=1500 after commit");

        bank.commit(old_reader);
        bank.commit(new_reader);
        assert_invariants(bank, "scenario 1");
    }

    // ─────────────────────────────────────────────────────────────────────────
    banner("2) Tombstone Visibility");
    // ─────────────────────────────────────────────────────────────────────────
    // Deleting a key leaves it readable by earlier snapshots.
    {
        txn_id_t pre_delete = bank.begin();
        verify(safe_read(bank, pre_delete, "carol") == "750",
               "pre_delete reader sees carol=750");

        txn_id_t deleter = bank.begin();
        verify(bank.remove(deleter, "carol") == Result::Ok, "deleter removes carol");
        verify(bank.commit(deleter) == Result::Ok, "delete committed");

        // pre_delete snapshot predates the delete; carol still exists there.
        verify(safe_read(bank, pre_delete, "carol") == "750",
               "pre_delete snapshot still sees carol=750");

        // A snapshot taken after the delete must not see carol.
        txn_id_t post_delete = bank.begin();
        verify(safe_read(bank, post_delete, "carol") == "<none>",
               "post_delete snapshot sees carol as <none>");

        bank.commit(pre_delete);
        bank.commit(post_delete);
        assert_invariants(bank, "scenario 2");
    }

    // ─────────────────────────────────────────────────────────────────────────
    banner("3) Strict 2PL — Lock Contention");
    // ─────────────────────────────────────────────────────────────────────────
    {
        txn_id_t t1 = bank.begin();
        txn_id_t t2 = bank.begin();

        verify(bank.write(t1, "bob", "600") == Result::Ok,
               "T1 acquires X-lock on bob");
        verify(bank.write(t2, "bob", "700") == Result::LockWait,
               "T2 blocked: LOCK_WAIT on bob");

        // Abort T1 to release the lock.
        bank.abort(t1);
        verify(bank.state_of(t1) == State::Aborted, "T1 aborted, lock released");

        // T2 can now acquire the lock.
        verify(bank.write(t2, "bob", "700") == Result::Ok,
               "T2 acquires bob after T1 aborts");
        verify(bank.commit(t2) == Result::Ok, "T2 commits bob=700");

        txn_id_t verify_txn = bank.begin();
        verify(safe_read(bank, verify_txn, "bob") == "700", "confirmed bob=700");
        bank.commit(verify_txn);
        assert_invariants(bank, "scenario 3");
    }

    // ─────────────────────────────────────────────────────────────────────────
    banner("4) Deadlock Detection");
    // ─────────────────────────────────────────────────────────────────────────
    // T_ab: alice → bob  |  T_ba: bob → alice  → cycle
    {
        txn_id_t t_ab = bank.begin();
        txn_id_t t_ba = bank.begin();

        verify(bank.write(t_ab, "alice", "1400") == Result::Ok,
               "T_ab locks alice");
        verify(bank.write(t_ba, "bob",   "650")  == Result::Ok,
               "T_ba locks bob");

        // T_ab now tries to lock bob (held by T_ba) → LOCK_WAIT.
        verify(bank.write(t_ab, "bob", "750") == Result::LockWait,
               "T_ab waits for bob (held by T_ba)");

        // T_ba tries to lock alice (held by T_ab) → cycle detected.
        Result deadlock_result = bank.write(t_ba, "alice", "1600");
        std::cout << "  T_ba wants alice -> " << to_string(deadlock_result)
                  << "  victim=T" << bank.last_victim() << "\n";

        verify(deadlock_result == Result::Aborted,
               "deadlock detected: T_ba (younger) is the victim");
        verify(bank.last_victim() == t_ba, "victim ID is T_ba");
        verify(bank.state_of(t_ba) == State::Aborted, "T_ba state = Aborted");

        // T_ab can now proceed to lock bob (T_ba released it on abort).
        verify(bank.write(t_ab, "bob", "750") == Result::Ok,
               "T_ab acquires bob after T_ba aborted");
        verify(bank.commit(t_ab) == Result::Ok, "T_ab commits");
        assert_invariants(bank, "scenario 4");
    }

    // ─────────────────────────────────────────────────────────────────────────
    banner("5) First-Updater-Wins");
    // ─────────────────────────────────────────────────────────────────────────
    {
        txn_id_t t1 = bank.begin();
        txn_id_t t2 = bank.begin();

        verify(safe_read(bank, t1, "alice") == "1400", "T1 reads alice=1400");
        verify(safe_read(bank, t2, "alice") == "1400", "T2 reads alice=1400");

        verify(bank.write(t1, "alice", "2000") == Result::Ok,
               "T1 writes alice=2000 (holds lock)");
        verify(bank.write(t2, "alice", "100") == Result::LockWait,
               "T2 blocked on alice (held by T1)");

        verify(bank.commit(t1) == Result::Ok, "T1 commits alice=2000");

        // T2 can acquire the lock now, but its snapshot is stale.
        verify(bank.write(t2, "alice", "100") == Result::Ok,
               "T2 acquires alice lock after T1 releases");

        Result r = bank.commit(t2);
        std::cout << "  T2 commit result -> " << to_string(r) << "\n";
        verify(r == Result::SerializationFailure,
               "T2 rejected: alice changed after T2's snapshot");

        txn_id_t check = bank.begin();
        verify(safe_read(bank, check, "alice") == "2000",
               "alice = 2000 (first updater won)");
        bank.commit(check);
        assert_invariants(bank, "scenario 5");
    }

    // ─────────────────────────────────────────────────────────────────────────
    banner("6) Garbage Collection");
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "  alice chain before gc: " << bank.dump_chain("alice") << "\n";
        std::size_t before  = bank.version_count();
        std::size_t pruned  = bank.gc();
        std::size_t after   = bank.version_count();

        std::cout << "  versions: " << before << " -> " << after
                  << "  (pruned " << pruned << ")\n";
        std::cout << "  alice chain after  gc: " << bank.dump_chain("alice") << "\n";

        verify(pruned > 0, "gc() reclaimed at least one obsolete version");
        verify(before - pruned == after, "version count is consistent post-gc");

        txn_id_t v = bank.begin();
        verify(safe_read(bank, v, "alice") == "2000", "alice=2000 survives gc");
        verify(safe_read(bank, v, "bob")   == "750",  "bob=750 survives gc");
        bank.commit(v);
        assert_invariants(bank, "scenario 6");
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\nFinal stats: "
              << bank.live_txn_count() << " live txns, "
              << bank.lock_count()     << " held locks, "
              << bank.version_count()  << " versions.\n";
    std::cout << "All transaction-manager scenarios passed.\n";
    return 0;
}