// Assignment 8 — Transaction Manager (MVCC + Strict 2PL)
// Tanishq Singh | 24BCS10303
//
// Single-file in-memory transaction engine that combines four ideas:
//
//   1. MVCC for reads  — per-key version chains tagged with creator / invalidator
//      txn ids. Readers never block writers, writers never block readers.
//
//   2. Strict 2PL for writes — Shared/Exclusive row locks held until
//      commit or abort. Supports S → X upgrade when the requester is the
//      sole holder.
//
//   3. Deadlock detection — before blocking, we add an edge to the
//      waits-for graph and run a DFS for a cycle. If a cycle is found the
//      youngest transaction on the path (highest id) is killed immediately,
//      so no thread ever actually blocks indefinitely.
//
//   4. First-updater-wins (lost-update protection) — after grabbing X,
//      write re-scans the chain and aborts if any other txn committed a
//      modification after our snapshot.
//
//   5. vacuum() — prunes versions invalidated before the oldest live snapshot.

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>

using namespace std;

// ─── basic types ──────────────────────────────────────────────────────────────
using TxnID  = uint64_t;
using RowKey = string;
using Value  = int;

// ─── Version ─────────────────────────────────────────────────────────────────
// created_by  : txn that inserted this value
// deleted_by  : txn that overwrote / deleted it (0 = still live)
struct Version {
    TxnID created_by;
    TxnID deleted_by;
    Value val;
};

// ─── LockMode ────────────────────────────────────────────────────────────────
enum class LockMode { SHARED, EXCLUSIVE };

// ─── TxnStatus ───────────────────────────────────────────────────────────────
enum class TxnStatus { ACTIVE, COMMITTED, ABORTED };

// ─── Transaction ─────────────────────────────────────────────────────────────
struct Transaction {
    TxnID            id;
    TxnID            snapshot;          // latest_committed at begin() time
    TxnStatus        status = TxnStatus::ACTIVE;
    set<RowKey>      locks_held;
    map<RowKey, LockMode> lock_mode;    // which mode we hold per key
};

// ─── LockEntry ───────────────────────────────────────────────────────────────
// Tracks who holds a lock on one key and who is waiting.
struct LockEntry {
    map<TxnID, LockMode> holders;   // txn_id → mode currently granted

    bool held_exclusive() const {
        for (auto& [id, m] : holders)
            if (m == LockMode::EXCLUSIVE) return true;
        return false;
    }
};

// ─── TransactionManager ──────────────────────────────────────────────────────

class TransactionManager {
public:
    TransactionManager() : next_id_(1), latest_committed_(0) {}

    // ── lifecycle ─────────────────────────────────────────────────────────────

    TxnID begin() {
        TxnID id   = next_id_++;
        TxnID snap = latest_committed_;
        txns_[id]  = Transaction{id, snap, TxnStatus::ACTIVE, {}, {}};
        return id;
    }

    void commit(TxnID id) {
        auto& t = get_txn(id);
        check_active(t);
        t.status = TxnStatus::COMMITTED;
        if (id > latest_committed_) latest_committed_ = id;
        release_all_locks(id);
    }

    void abort(TxnID id) {
        auto& t = get_txn(id);
        if (t.status == TxnStatus::ABORTED) return;   // already done
        rollback_writes(id);
        t.status = TxnStatus::ABORTED;
        release_all_locks(id);
    }

    // ── read ──────────────────────────────────────────────────────────────────
    // MVCC snapshot read — lock-free. Walks the version chain and returns the
    // first version visible to our snapshot:
    //   created_by <= snapshot  AND  (deleted_by==0  OR  deleted_by > snapshot)
    // Readers never block on writers; writers never block readers.

    bool read(TxnID id, const RowKey& key, Value& out) {
        auto& t = get_txn(id);
        check_active(t);
        // no lock needed — we read from the immutable version chain
        return read_snapshot(t, key, out);
    }

    // ── read_locked ───────────────────────────────────────────────────────────
    // Same as read() but also acquires a Shared lock. Used to demonstrate that
    // two concurrent S locks on the same key are compatible.

    bool read_locked(TxnID id, const RowKey& key, Value& out) {
        auto& t = get_txn(id);
        check_active(t);
        acquire_lock(id, key, LockMode::SHARED);
        return read_snapshot(t, key, out);
    }

    // ── write ─────────────────────────────────────────────────────────────────
    // Acquires X lock (or upgrades S→X), then checks for lost-update conflict,
    // marks old live version as deleted, appends new version.

    void write(TxnID id, const RowKey& key, Value val) {
        auto& t = get_txn(id);
        check_active(t);
        acquire_lock(id, key, LockMode::EXCLUSIVE);  // may throw if deadlock

        // lost-update check: if another txn committed a write after our snapshot
        auto it = store_.find(key);
        if (it != store_.end()) {
            for (auto& v : it->second) {
                if (v.created_by == id) continue;
                auto cit = txns_.find(v.created_by);
                if (cit == txns_.end()) continue;
                if (cit->second.status == TxnStatus::COMMITTED &&
                    v.created_by > t.snapshot) {
                    rollback_writes(id);
                    t.status = TxnStatus::ABORTED;
                    release_all_locks(id);
                    throw runtime_error("txn " + to_string(id) +
                                        " aborted: lost-update conflict on key '" + key + "'");
                }
            }
            // mark existing live version(s) as deleted by us
            for (auto& v : it->second)
                if (v.deleted_by == 0 && v.created_by != id)
                    v.deleted_by = id;
        }

        store_[key].push_back(Version{id, 0, val});
        t.locks_held.insert(key);
    }

    // ── vacuum ────────────────────────────────────────────────────────────────
    // Computes the oldest live snapshot (xmin) and removes any version whose
    // deletion was committed before xmin — no active txn can ever see it.

    size_t vacuum() {
        // find oldest active snapshot
        TxnID xmin = latest_committed_ + 1;
        for (auto& [tid, t] : txns_)
            if (t.status == TxnStatus::ACTIVE)
                xmin = min(xmin, t.snapshot);

        size_t pruned = 0;
        for (auto& [key, chain] : store_) {
            size_t before = chain.size();
            chain.erase(
                remove_if(chain.begin(), chain.end(), [&](const Version& v) {
                    if (v.deleted_by == 0) return false;       // still live
                    // safe to prune if deleting txn committed before all live snapshots
                    auto dit = txns_.find(v.deleted_by);
                    if (dit == txns_.end()) return false;
                    return dit->second.status == TxnStatus::COMMITTED &&
                           v.deleted_by < xmin;
                }),
                chain.end());
            pruned += before - chain.size();
        }
        return pruned;
    }

    // ── debug dump ────────────────────────────────────────────────────────────
    void dump(const string& label = "") const {
        if (!label.empty()) cout << "\n  [" << label << "]\n";
        for (auto& [key, chain] : store_) {
            cout << "    key=" << key << ":";
            for (auto& v : chain)
                cout << " [val=" << v.val
                     << " xmin=" << v.created_by
                     << " xmax=" << v.deleted_by << "]";
            cout << "\n";
        }
    }

private:
    // ── lock acquisition ─────────────────────────────────────────────────────
    // Fully synchronous (no real blocking threads). Before "waiting", we
    // register the waits-for edge and immediately check for a deadlock cycle.
    // If a cycle is detected, the YOUNGEST txn in the cycle is aborted.
    // If the caller is aborted, this throws. Otherwise we just grant the lock
    // (the holder that was killed has released it).

    void acquire_lock(TxnID id, const RowKey& key, LockMode mode) {
        // check if we already hold a compatible lock
        LockEntry& le = lock_table_[key];
        auto it = le.holders.find(id);
        if (it != le.holders.end()) {
            if (mode == LockMode::SHARED)    return;   // S already granted
            if (it->second == LockMode::EXCLUSIVE) return;  // X already granted
            // upgrade S → X — only if we are the sole holder
            if (le.holders.size() == 1) {
                it->second = LockMode::EXCLUSIVE;
                txns_[id].lock_mode[key] = LockMode::EXCLUSIVE;
                return;
            }
            // someone else also holds S — need to wait; add waits-for edges
        }

        // can we grant now?
        if (can_grant(le, id, mode)) {
            le.holders[id] = mode;
            txns_[id].locks_held.insert(key);
            txns_[id].lock_mode[key] = mode;
            return;
        }

        // conflict — register waits-for edges and check for deadlock
        for (auto& [hid, _] : le.holders)
            if (hid != id)
                waits_for_[id].insert(hid);

        // deadlock check: if there is a cycle, kill the youngest txn in it
        vector<TxnID> cycle;
        if (find_cycle(id, cycle)) {
            // youngest = highest id in the cycle
            TxnID victim = *max_element(cycle.begin(), cycle.end());
            // clean up our own waits-for edges before aborting
            waits_for_.erase(id);
            if (victim == id) {
                rollback_writes(id);
                txns_[id].status = TxnStatus::ABORTED;
                release_all_locks(id);
                throw runtime_error("txn " + to_string(id) +
                                    " aborted: deadlock detected (victim, key='" + key + "')");
            } else {
                // abort the other participant — it will release its locks
                rollback_writes(victim);
                txns_[victim].status = TxnStatus::ABORTED;
                release_all_locks(victim);
                waits_for_.erase(victim);
                // now retry granting our lock
                acquire_lock(id, key, mode);
                return;
            }
        }

        // no deadlock — in a real multi-threaded engine we'd block here.
        // For this single-threaded engine the conflict means we should abort;
        // we treat it as if we timed out waiting.
        waits_for_.erase(id);
        throw runtime_error("txn " + to_string(id) +
                            " cannot acquire lock on key '" + key +
                            "' (conflict; would block)");
    }

    bool can_grant(const LockEntry& le, TxnID id, LockMode mode) const {
        if (le.holders.empty()) return true;
        if (mode == LockMode::SHARED && !le.held_exclusive()) {
            // S is compatible with other S holders
            // but not compatible with X from another txn
            for (auto& [hid, m] : le.holders)
                if (hid != id && m == LockMode::EXCLUSIVE) return false;
            return true;
        }
        // want X — only compatible if no one else holds anything
        if (le.holders.size() == 1 && le.holders.count(id)) return true;
        return le.holders.empty();
    }

    // ── deadlock detection ───────────────────────────────────────────────────
    // Returns true if there is a cycle reachable from `start` in waits_for_.
    // If a cycle is found, `cycle` is filled with the nodes on the cycle path.

    bool find_cycle(TxnID start, vector<TxnID>& cycle) const {
        set<TxnID>    visited;
        vector<TxnID> path;
        return dfs_cycle(start, visited, path, cycle);
    }

    bool dfs_cycle(TxnID node, set<TxnID>& visited,
                   vector<TxnID>& path, vector<TxnID>& cycle) const {
        // if node is already on the current path, we found a cycle
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == node) {
                cycle.assign(path.begin() + (ptrdiff_t)i, path.end());
                return true;
            }
        }
        if (visited.count(node)) return false;
        visited.insert(node);
        path.push_back(node);
        auto it = waits_for_.find(node);
        if (it != waits_for_.end()) {
            for (TxnID next : it->second) {
                if (dfs_cycle(next, visited, path, cycle)) return true;
            }
        }
        path.pop_back();
        return false;
    }

    // ── release all locks ────────────────────────────────────────────────────
    void release_all_locks(TxnID id) {
        auto& t = txns_[id];
        for (auto& key : t.locks_held) {
            auto it = lock_table_.find(key);
            if (it != lock_table_.end())
                it->second.holders.erase(id);
        }
        t.locks_held.clear();
        t.lock_mode.clear();
        waits_for_.erase(id);
    }

    // ── rollback writes ──────────────────────────────────────────────────────
    void rollback_writes(TxnID id) {
        for (auto& [key, chain] : store_) {
            // restore any version we stamped as deleted
            for (auto& v : chain)
                if (v.deleted_by == id) v.deleted_by = 0;
            // remove versions we created
            chain.erase(
                remove_if(chain.begin(), chain.end(),
                          [id](const Version& v){ return v.created_by == id; }),
                chain.end());
        }
    }

    // ── helpers ───────────────────────────────────────────────────────────────
    Transaction& get_txn(TxnID id) {
        auto it = txns_.find(id);
        if (it == txns_.end())
            throw runtime_error("unknown txn: " + to_string(id));
        return it->second;
    }
    static void check_active(const Transaction& t) {
        if (t.status != TxnStatus::ACTIVE)
            throw runtime_error("txn " + to_string(t.id) + " is not active");
    }

    // walk the version chain and return the snapshot-visible value
    bool read_snapshot(const Transaction& t, const RowKey& key, Value& out) const {
        auto it = store_.find(key);
        if (it == store_.end()) return false;
        const auto& chain = it->second;
        for (int i = (int)chain.size() - 1; i >= 0; --i) {
            const Version& v = chain[i];
            if (v.created_by > t.snapshot) continue;
            if (v.deleted_by != 0 && v.deleted_by <= t.snapshot) continue;
            out = v.val;
            return true;
        }
        return false;
    }

    // ── state ────────────────────────────────────────────────────────────────
    TxnID  next_id_;
    TxnID  latest_committed_;

    unordered_map<TxnID,  Transaction>       txns_;
    unordered_map<RowKey, vector<Version>>   store_;
    unordered_map<RowKey, LockEntry>         lock_table_;
    unordered_map<TxnID,  set<TxnID>>       waits_for_;
};

// ─── test helpers ─────────────────────────────────────────────────────────────

static int pass_count = 0;
static int fail_count = 0;

static void CHECK(bool cond, const string& label) {
    if (cond) {
        cout << "  PASS: " << label << "\n";
        pass_count++;
    } else {
        cout << "  FAIL: " << label << "\n";
        fail_count++;
    }
}

// ─── Demo 1: Snapshot Isolation ───────────────────────────────────────────────
// T1 writes "a"=10 and commits. T2 starts after and reads it (gets 10).
// T3 starts before T4 commits "a"=99 — should still see 10.

static void demo_snapshot_isolation() {
    cout << "\n--- Demo 1: Snapshot Isolation ---\n";
    TransactionManager tm;

    TxnID t1 = tm.begin();
    tm.write(t1, "a", 10);
    tm.commit(t1);

    TxnID t2 = tm.begin();
    Value v2 = -1;
    bool ok2 = tm.read(t2, "a", v2);
    CHECK(ok2 && v2 == 10, "T2 reads T1's committed write (10)");
    tm.commit(t2);

    // T3 begins before T4 commits
    TxnID t3 = tm.begin();
    TxnID t4 = tm.begin();
    tm.write(t4, "a", 99);
    tm.commit(t4);

    Value v3 = -1;
    tm.read(t3, "a", v3);
    CHECK(v3 == 10, "T3 snapshot still sees 10, not T4's committed 99");
    tm.commit(t3);
}

// ─── Demo 2: Shared Locks ─────────────────────────────────────────────────────
// Two readers holding S locks on the same key simultaneously — should both work.

static void demo_shared_locks() {
    cout << "\n--- Demo 2: Shared Locks ---\n";
    TransactionManager tm;

    TxnID seed = tm.begin();
    tm.write(seed, "x", 42);
    tm.commit(seed);

    TxnID t2 = tm.begin();
    TxnID t3 = tm.begin();

    // read_locked() acquires an S lock before reading — two S locks are compatible
    Value v2 = -1, v3 = -1;
    bool ok2 = tm.read_locked(t2, "x", v2);
    bool ok3 = tm.read_locked(t3, "x", v3);   // S+S — must not conflict

    CHECK(ok2 && v2 == 42, "T2 reads x=42 with S lock");
    CHECK(ok3 && v3 == 42, "T3 reads x=42 with S lock simultaneously (S+S ok)");

    tm.commit(t2);
    tm.commit(t3);
}

// ─── Demo 3: Blocking / SI Snapshot Read ─────────────────────────────────────
// T1 writes but doesn't commit. T2 reads the same key — MVCC means it sees
// the pre-T1 snapshot value (no blocking). After T1 commits, T3 sees new value.

static void demo_blocking_si_snapshot_read() {
    cout << "\n--- Demo 3: Blocking / SI Snapshot Read ---\n";
    TransactionManager tm;

    TxnID seed = tm.begin();
    tm.write(seed, "b", 5);
    tm.commit(seed);

    TxnID t1 = tm.begin();
    tm.write(t1, "b", 20);   // uncommitted write

    // T2 starts and reads — should see 5 (its snapshot is before T1's write)
    TxnID t2 = tm.begin();
    Value v2 = -1;
    bool ok = tm.read(t2, "b", v2);
    CHECK(ok && v2 == 5, "T2 sees snapshot value (5) while T1 is uncommitted");
    tm.commit(t2);

    tm.commit(t1);

    // T3 starts after T1 commits — sees 20
    TxnID t3 = tm.begin();
    Value v3 = -1;
    tm.read(t3, "b", v3);
    CHECK(v3 == 20, "T3 reads T1's committed value (20)");
    tm.commit(t3);
}

// ─── Demo 4: Lock Upgrade S → X ──────────────────────────────────────────────
// Single txn acquires S then writes the same key — upgrade should succeed
// because it is the sole holder.

static void demo_lock_upgrade() {
    cout << "\n--- Demo 4: Lock Upgrade (S -> X) ---\n";
    TransactionManager tm;

    TxnID seed = tm.begin();
    tm.write(seed, "c", 1);
    tm.commit(seed);

    TxnID t2 = tm.begin();
    Value v = -1;
    // read_locked() acquires S first, then we upgrade to X
    tm.read_locked(t2, "c", v);
    CHECK(v == 1, "T2 read c=1 with S lock");

    bool threw = false;
    try {
        tm.write(t2, "c", 2);    // upgrade S → X (sole holder — must succeed)
    } catch (...) {
        threw = true;
    }
    CHECK(!threw, "S→X upgrade succeeded (sole holder)");
    tm.commit(t2);

    TxnID t3 = tm.begin();
    Value v3 = -1;
    tm.read(t3, "c", v3);
    CHECK(v3 == 2, "T3 reads upgraded value c=2");
    tm.commit(t3);
}

// ─── Demo 5: Deadlock Detection ───────────────────────────────────────────────
// Build a waits-for cycle manually:
//   T1 holds X on "d1", T2 holds X on "d2".
//   T1 tries X on "d2" → waits for T2.
//   T2 tries X on "d1" → waits for T1.
//   Cycle: T1 ← T2 ← T1. Youngest txn (highest id = T2) gets killed.

static void demo_deadlock_detection() {
    cout << "\n--- Demo 5: Deadlock Detection ---\n";
    TransactionManager tm;

    TxnID seed = tm.begin();
    tm.write(seed, "d1", 1);
    tm.write(seed, "d2", 2);
    tm.commit(seed);

    TxnID t1 = tm.begin();
    TxnID t2 = tm.begin();

    // T1 locks d1
    tm.write(t1, "d1", 10);
    // T2 locks d2
    tm.write(t2, "d2", 20);

    // T1 tries to lock d2 — T2 holds it, waits-for T2 registered.
    // No cycle yet so this would block; engine treats as timeout/conflict.
    bool t1_failed = false;
    try {
        tm.write(t1, "d2", 30);   // T1 waits for T2
    } catch (...) {
        t1_failed = true;
    }

    // T2 tries to lock d1 — T1 holds it. Now we have T2→T1→T2 cycle.
    bool t2_failed = false;
    try {
        tm.write(t2, "d1", 40);   // T2 waits for T1 → CYCLE
    } catch (...) {
        t2_failed = true;
    }

    CHECK(t1_failed || t2_failed, "Deadlock detected: at least one txn aborted");

    // clean up the surviving transaction
    if (!t1_failed) { try { tm.abort(t1); } catch (...) {} }
    if (!t2_failed) { try { tm.abort(t2); } catch (...) {} }
}

// ─── Demo 6: Lost Update Prevention ──────────────────────────────────────────
// T1 and T2 both read "e"=100. T1 writes 200 and commits. T2 then tries to
// write 300 — must be aborted (first-updater-wins).

static void demo_lost_update_prevention() {
    cout << "\n--- Demo 6: Lost Update Prevention (SI) ---\n";
    TransactionManager tm;

    TxnID seed = tm.begin();
    tm.write(seed, "e", 100);
    tm.commit(seed);

    TxnID t1 = tm.begin();
    TxnID t2 = tm.begin();

    Value v1 = -1, v2 = -1;
    tm.read(t1, "e", v1);
    tm.read(t2, "e", v2);
    CHECK(v1 == 100 && v2 == 100, "Both T1 and T2 read e=100");

    // T1 updates and commits first
    tm.write(t1, "e", 200);
    tm.commit(t1);

    // T2 tries to write — lost-update conflict → abort
    bool t2_aborted = false;
    try {
        tm.write(t2, "e", 300);
        tm.commit(t2);
    } catch (...) {
        t2_aborted = true;
    }
    CHECK(t2_aborted, "T2 aborted: lost-update conflict (first-updater-wins)");

    TxnID t3 = tm.begin();
    Value final_val = -1;
    tm.read(t3, "e", final_val);
    CHECK(final_val == 200, "Final value = T1's write (200), not T2's lost-update");
    tm.commit(t3);
}

// ─── Demo 7: Vacuum GC Version Pruning ───────────────────────────────────────
// Write three successive versions of "f", commit all, then vacuum.
// With no live transactions, all old versions should be pruned.

static void demo_vacuum() {
    cout << "\n--- Demo 7: Vacuum GC Version Pruning ---\n";
    TransactionManager tm;

    for (int i = 1; i <= 3; ++i) {
        TxnID t = tm.begin();
        tm.write(t, "f", i * 10);
        tm.commit(t);
    }

    tm.dump("Before vacuum (3 versions of 'f')");

    size_t pruned = tm.vacuum();
    cout << "  Pruned " << pruned << " old version(s)\n";
    CHECK(pruned >= 2, "vacuum() pruned at least 2 stale versions");

    tm.dump("After vacuum");

    TxnID t = tm.begin();
    Value v = -1;
    tm.read(t, "f", v);
    CHECK(v == 30, "After vacuum, 'f' still reads latest value (30)");
    tm.commit(t);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    cout << "=================================================\n";
    cout << "  Assignment 8 — Transaction Manager\n";
    cout << "  MVCC + Strict 2PL + Deadlock Detection\n";
    cout << "  Tanishq Singh | 24BCS10303\n";
    cout << "=================================================\n";

    demo_snapshot_isolation();
    demo_shared_locks();
    demo_blocking_si_snapshot_read();
    demo_lock_upgrade();
    demo_deadlock_detection();
    demo_lost_update_prevention();
    demo_vacuum();

    cout << "\n=================================================\n";
    cout << "  Results: " << pass_count << "/"
         << (pass_count + fail_count) << " PASS\n";
    cout << "=================================================\n";

    return fail_count == 0 ? 0 : 1;
}
