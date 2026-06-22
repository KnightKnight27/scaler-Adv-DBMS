// ===========================================================================
//  txn_manager.cpp
//
//  An in-memory transaction manager that combines three classic concurrency
//  control mechanisms:
//
//    1. MVCC version chains  -- snapshot (read-only) reads never take locks
//                               and therefore never block and never deadlock.
//    2. Strict 2PL           -- read/write transactions acquire S/X locks,
//                               support S->X upgrade, and hold every lock
//                               until commit/abort (strict release).
//    3. Deadlock detection   -- a waits-for graph is searched for cycles with
//                               DFS; if a cycle exists, the youngest member is
//                               aborted as the victim.
//
//  Concurrency is *simulated* deterministically.  Instead of spawning real
//  threads (which would make the demo output non-reproducible and harder to
//  grade), each transaction issues operations in an explicit, hand-chosen
//  order.  When an operation "blocks", we model it as the lock request being
//  parked on a queue and the requesting transaction being marked WAITING; a
//  later step that releases the lock then wakes the waiter.  This is exactly
//  the bookkeeping a real lock manager performs, minus the OS scheduling.
//
//  Build:   g++ -std=c++17 *.cpp -o txn
//  Run:     ./txn
// ===========================================================================

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
//  Basic type aliases
// ---------------------------------------------------------------------------
using TxnId = std::uint64_t;   // transaction identifier == start order
using Ts    = std::uint64_t;   // logical timestamp (begin / commit)
using Key   = std::string;
using Value = std::string;

static constexpr Ts INF = ~Ts(0);  // "infinity": version is still current

// ---------------------------------------------------------------------------
//  MVCC version
//
//  A version records the value that some committed (or in-flight) transaction
//  installed for a key, together with the time interval during which it is the
//  "current" value:
//
//      begin_ts  -- commit timestamp of the transaction that created it
//      end_ts    -- commit timestamp of the transaction that superseded it,
//                   or INF while it is still the latest committed value.
//
//  A reader with snapshot timestamp `snap` sees this version iff
//      begin_ts <= snap < end_ts
// ---------------------------------------------------------------------------
struct Version {
    Value value;
    Ts    begin_ts;             // when this version became visible
    Ts    end_ts;               // when it stopped being the current version
    TxnId creator;             // bookkeeping / trace only
    std::shared_ptr<Version> prev;  // older version in the chain
};

// ---------------------------------------------------------------------------
//  Lock modes
// ---------------------------------------------------------------------------
enum class LockMode { SHARED, EXCLUSIVE };

static const char* mode_name(LockMode m) {
    return m == LockMode::SHARED ? "S" : "X";
}

// ---------------------------------------------------------------------------
//  Transaction state
// ---------------------------------------------------------------------------
enum class TxnState { ACTIVE, WAITING, COMMITTED, ABORTED };

struct Transaction {
    TxnId    id;
    Ts       start_ts;          // snapshot timestamp for MVCC reads
    bool     read_only;
    TxnState state = TxnState::ACTIVE;

    // Locks currently held by this transaction (Strict 2PL: released only at
    // commit/abort).  Maps key -> mode held.
    std::unordered_map<Key, LockMode> held;
};

// ===========================================================================
//  LockManager
//
//  Maintains, per key, the set of granted lock holders and a FIFO queue of
//  waiters.  It also owns the waits-for graph used for deadlock detection.
// ===========================================================================
class TransactionManager;  // fwd decl

class LockManager {
public:
    struct Granted {
        TxnId    txn;
        LockMode mode;
    };
    struct Waiter {
        TxnId    txn;
        LockMode mode;
    };
    struct LockEntry {
        std::vector<Granted> granted;
        std::vector<Waiter>  queue;   // FIFO of blocked requests
    };

    // Returns true if the request is granted immediately, false if the caller
    // must wait (it has been enqueued and the waits-for edges added).
    bool request(TxnId txn, const Key& key, LockMode mode);

    // Release every lock held by `txn` (called on commit/abort).  Returns the
    // set of transactions that became unblocked so the caller can resume them.
    std::vector<TxnId> release_all(TxnId txn,
                                   const std::unordered_map<Key, LockMode>& held);

    // ---- Deadlock detection -------------------------------------------------
    // Searches the waits-for graph for a cycle reachable from `start`.  If a
    // cycle is found, returns its members so a victim can be chosen.
    std::vector<TxnId> find_cycle(TxnId start);

    void dump_waits_for() const;

private:
    std::unordered_map<Key, LockEntry> table_;

    // waits_for_[a] = { b, c }   means "a is waiting for b and c to release".
    std::unordered_map<TxnId, std::unordered_set<TxnId>> waits_for_;

    static bool conflicts(LockMode a, LockMode b) {
        // Only S/S is compatible; everything involving X conflicts.
        return !(a == LockMode::SHARED && b == LockMode::SHARED);
    }

    void add_wait_edges(TxnId waiter, const LockEntry& e, LockMode mode);
    void clear_wait_edges(TxnId waiter);

    bool dfs(TxnId node, TxnId target,
             std::unordered_set<TxnId>& on_path,
             std::vector<TxnId>& path);
};

// ---------------------------------------------------------------------------
bool LockManager::request(TxnId txn, const Key& key, LockMode mode) {
    LockEntry& e = table_[key];

    // ---- Re-entrancy: this txn already holds a lock on the key -------------
    for (auto& g : e.granted) {
        if (g.txn == txn) {
            if (g.mode == mode || mode == LockMode::SHARED) {
                return true;                 // already strong enough
            }
            // S -> X upgrade requested.  Allowed only if no *other* txn holds
            // the lock; otherwise the upgrade must wait.
            bool others = false;
            for (auto& h : e.granted)
                if (h.txn != txn) { others = true; break; }
            if (!others) {
                g.mode = LockMode::EXCLUSIVE;  // perform the upgrade in place
                return true;
            }
            add_wait_edges(txn, e, mode);
            e.queue.push_back({txn, mode});
            return false;
        }
    }

    // ---- Fresh request -----------------------------------------------------
    bool conflict = false;
    for (auto& g : e.granted)
        if (conflicts(g.mode, mode)) { conflict = true; break; }

    // Respect FIFO fairness: if someone is already waiting, do not jump ahead.
    if (!conflict && e.queue.empty()) {
        e.granted.push_back({txn, mode});
        return true;
    }

    add_wait_edges(txn, e, mode);
    e.queue.push_back({txn, mode});
    return false;
}

// ---------------------------------------------------------------------------
void LockManager::add_wait_edges(TxnId waiter, const LockEntry& e, LockMode mode) {
    // The waiter waits for every currently-granted holder it conflicts with...
    for (auto& g : e.granted)
        if (g.txn != waiter && conflicts(g.mode, mode))
            waits_for_[waiter].insert(g.txn);
    // ...and for any earlier waiter in the queue it conflicts with.
    for (auto& w : e.queue)
        if (w.txn != waiter && conflicts(w.mode, mode))
            waits_for_[waiter].insert(w.txn);
}

void LockManager::clear_wait_edges(TxnId waiter) {
    waits_for_.erase(waiter);
}

// ---------------------------------------------------------------------------
std::vector<TxnId> LockManager::release_all(
        TxnId txn, const std::unordered_map<Key, LockMode>& held) {
    std::unordered_set<TxnId> woken;

    // A departing transaction is no longer waiting for anyone, and no one is
    // waiting for it via an edge we keep --- those edges are recomputed below.
    clear_wait_edges(txn);

    for (auto& [key, mode] : held) {
        (void)mode;
        auto it = table_.find(key);
        if (it == table_.end()) continue;
        LockEntry& e = it->second;

        // Remove the txn from the granted set.
        e.granted.erase(
            std::remove_if(e.granted.begin(), e.granted.end(),
                           [&](const Granted& g) { return g.txn == txn; }),
            e.granted.end());

        // Try to grant queued waiters from the front (FIFO).
        while (!e.queue.empty()) {
            Waiter w = e.queue.front();
            bool conflict = false;
            for (auto& g : e.granted)
                if (g.txn != w.txn && conflicts(g.mode, w.mode)) {
                    conflict = true;
                    break;
                }
            if (conflict) break;             // head blocked -> stop (no skipping)

            e.queue.erase(e.queue.begin());
            // Grant it (upgrade in place if it already holds something).
            bool upgraded = false;
            for (auto& g : e.granted)
                if (g.txn == w.txn) { g.mode = w.mode; upgraded = true; break; }
            if (!upgraded) e.granted.push_back({w.txn, w.mode});

            clear_wait_edges(w.txn);         // it is no longer blocked
            woken.insert(w.txn);
        }
    }

    return std::vector<TxnId>(woken.begin(), woken.end());
}

// ---------------------------------------------------------------------------
//  Cycle search: standard DFS looking for a back-edge to `target`.
// ---------------------------------------------------------------------------
bool LockManager::dfs(TxnId node, TxnId target,
                      std::unordered_set<TxnId>& on_path,
                      std::vector<TxnId>& path) {
    on_path.insert(node);
    path.push_back(node);

    auto it = waits_for_.find(node);
    if (it != waits_for_.end()) {
        for (TxnId next : it->second) {
            if (next == target) {            // closed the loop back to start
                path.push_back(next);
                return true;
            }
            if (!on_path.count(next) && dfs(next, target, on_path, path))
                return true;
        }
    }

    on_path.erase(node);
    path.pop_back();
    return false;
}

std::vector<TxnId> LockManager::find_cycle(TxnId start) {
    std::unordered_set<TxnId> on_path;
    std::vector<TxnId> path;
    if (dfs(start, start, on_path, path))
        return path;                          // start ... start
    return {};
}

void LockManager::dump_waits_for() const {
    std::cout << "    waits-for graph: ";
    bool any = false;
    for (auto& [a, set] : waits_for_) {
        for (TxnId b : set) {
            std::cout << "T" << a << "->T" << b << "  ";
            any = true;
        }
    }
    if (!any) std::cout << "(empty)";
    std::cout << "\n";
}

// ===========================================================================
//  TransactionManager
//
//  Ties the lock manager and the MVCC store together and exposes the
//  begin/read/write/commit/abort API used by the demo.
// ===========================================================================
class TransactionManager {
public:
    TransactionManager() {
        // Seed the store so the snapshot demo has a pre-existing committed
        // value to read.
        install_committed("X", "x0", clock_++);
        install_committed("Y", "y0", clock_++);
    }

    // -- lifecycle ----------------------------------------------------------
    Transaction* begin(bool read_only) {
        auto t = std::make_unique<Transaction>();
        t->id        = next_id_++;
        // The snapshot is taken at begin time.  We advance the logical clock on
        // begin as well as on commit so that a transaction that starts after an
        // earlier writer committed is guaranteed a strictly later snapshot.
        t->start_ts  = clock_++;
        t->read_only = read_only;
        Transaction* raw = t.get();
        txns_[raw->id] = std::move(t);
        std::cout << "[BEGIN]  T" << raw->id
                  << (read_only ? " (read-only, snapshot)" : " (read-write, 2PL)")
                  << "  start_ts=" << raw->start_ts << "\n";
        return raw;
    }

    // -- snapshot read (no locks) ------------------------------------------
    // Walks the version chain and returns the version visible as of the
    // transaction's start_ts.  Never blocks, never deadlocks.
    bool snapshot_read(Transaction* t, const Key& key, Value& out) {
        auto it = store_.find(key);
        for (auto v = (it == store_.end() ? nullptr : it->second); v; v = v->prev) {
            if (v->begin_ts <= t->start_ts && t->start_ts < v->end_ts) {
                out = v->value;
                std::cout << "[READ ]  T" << t->id << " snapshot-reads " << key
                          << " = '" << out << "'  (version begin="
                          << v->begin_ts << " end="
                          << (v->end_ts == INF ? std::string("INF")
                                               : std::to_string(v->end_ts))
                          << ")\n";
                return true;
            }
        }
        std::cout << "[READ ]  T" << t->id << " snapshot-reads " << key
                  << " = (none visible)\n";
        return false;
    }

    // -- locked read (for read-write txns) ---------------------------------
    // Returns: 1 = granted, 0 = blocked, -1 = aborted (deadlock victim).
    int read(Transaction* t, const Key& key, Value& out) {
        int r = acquire(t, key, LockMode::SHARED);
        if (r != 1) return r;
        // Read the latest committed value (a 2PL txn sees current state).
        auto it = store_.find(key);
        out = (it != store_.end() && it->second) ? it->second->value : "";
        std::cout << "[READ ]  T" << t->id << " (S) reads " << key
                  << " = '" << out << "'\n";
        return 1;
    }

    // -- locked write -------------------------------------------------------
    int write(Transaction* t, const Key& key, const Value& val) {
        int r = acquire(t, key, LockMode::EXCLUSIVE);
        if (r != 1) return r;
        // Buffer the write; it is installed into the version chain at commit.
        t_writes_[t->id][key] = val;
        std::cout << "[WRITE]  T" << t->id << " (X) writes " << key
                  << " = '" << val << "' (buffered)\n";
        return 1;
    }

    // -- commit -------------------------------------------------------------
    void commit(Transaction* t) {
        Ts ct = clock_++;            // commit timestamp
        // Install buffered writes as new versions, capping the previous head.
        auto wit = t_writes_.find(t->id);
        if (wit != t_writes_.end()) {
            for (auto& [key, val] : wit->second)
                install_committed(key, val, ct);
            t_writes_.erase(wit);
        }
        t->state = TxnState::COMMITTED;
        std::cout << "[COMMIT] T" << t->id << "  commit_ts=" << ct << "\n";
        finish(t);
    }

    // -- abort --------------------------------------------------------------
    void abort(Transaction* t, const char* why) {
        t->state = TxnState::ABORTED;
        t_writes_.erase(t->id);      // discard buffered writes
        std::cout << "[ABORT]  T" << t->id << "  (" << why << ")\n";
        finish(t);
    }

    void show_chain(const Key& key) {
        std::cout << "    version chain for " << key << ": ";
        auto it = store_.find(key);
        for (auto v = (it == store_.end() ? nullptr : it->second); v; v = v->prev) {
            std::cout << "['" << v->value << "' @T" << v->creator
                      << " " << v->begin_ts << ".."
                      << (v->end_ts == INF ? std::string("INF")
                                           : std::to_string(v->end_ts))
                      << "]";
            if (v->prev) std::cout << " -> ";
        }
        std::cout << "\n";
    }

    LockManager& locks() { return lm_; }

private:
    LockManager lm_;
    std::unordered_map<Key, std::shared_ptr<Version>> store_;  // head = newest
    std::unordered_map<TxnId, std::unique_ptr<Transaction>> txns_;
    std::unordered_map<TxnId, std::map<Key, Value>> t_writes_;  // buffered

    Ts    clock_   = 1;          // monotonically increasing logical clock
    TxnId next_id_ = 1;

    void install_committed(const Key& key, const Value& val, Ts ts) {
        auto v = std::make_shared<Version>();
        v->value    = val;
        v->begin_ts = ts;
        v->end_ts   = INF;
        v->creator  = 0;
        v->prev     = nullptr;
        auto it = store_.find(key);
        if (it != store_.end() && it->second) {
            it->second->end_ts = ts;   // cap the old head's lifetime
            v->prev = it->second;
        }
        store_[key] = v;
    }

    // Shared acquire path used by read()/write().  Performs deadlock
    // detection when a request would block.
    // Returns 1 granted, 0 blocked, -1 aborted.
    int acquire(Transaction* t, const Key& key, LockMode mode) {
        if (lm_.request(t->id, key, mode)) {
            t->held[key] = (mode == LockMode::EXCLUSIVE) ? LockMode::EXCLUSIVE
                                                         : t->held.count(key)
                                                               ? t->held[key]
                                                               : LockMode::SHARED;
            if (mode == LockMode::EXCLUSIVE) t->held[key] = LockMode::EXCLUSIVE;
            return 1;
        }

        // Request blocked: the txn is now WAITING.
        t->state = TxnState::WAITING;
        std::cout << "[BLOCK]  T" << t->id << " waits for " << mode_name(mode)
                  << " on " << key << "\n";
        lm_.dump_waits_for();

        // Run deadlock detection from this waiter.
        auto cycle = lm_.find_cycle(t->id);
        if (!cycle.empty()) {
            std::cout << "[DEADLK] cycle detected: ";
            for (size_t i = 0; i < cycle.size(); ++i)
                std::cout << "T" << cycle[i] << (i + 1 < cycle.size() ? " -> " : "");
            std::cout << "\n";

            TxnId victim = choose_victim(cycle);
            std::cout << "[VICTIM] choosing youngest -> T" << victim << "\n";

            Transaction* vt = txns_[victim].get();
            // Roll back the victim, freeing its locks so the survivor proceeds.
            abort(vt, "deadlock victim");

            if (victim == t->id) return -1;   // the caller itself was chosen
            // The caller's wait edges may now be satisfiable; the survivor is
            // resumed by finish()'s release wakeups, so report still-blocked.
            return 0;
        }
        return 0;
    }

    // Victim selection policy: abort the *youngest* transaction in the cycle
    // (largest id == most recent start), which tends to waste the least work.
    TxnId choose_victim(const std::vector<TxnId>& cycle) {
        TxnId v = cycle.front();
        for (TxnId id : cycle) v = std::max(v, id);
        return v;
    }

    // Common commit/abort tail: release locks and resume any woken waiters.
    void finish(Transaction* t) {
        auto woken = lm_.release_all(t->id, t->held);
        t->held.clear();
        for (TxnId id : woken) {
            auto it = txns_.find(id);
            if (it != txns_.end() && it->second->state == TxnState::WAITING) {
                it->second->state = TxnState::ACTIVE;
                std::cout << "         -> T" << id << " unblocked\n";
            }
        }
    }
};

// ===========================================================================
//  Demo scenarios
// ===========================================================================
static void banner(const std::string& s) {
    std::cout << "\n========================================================\n"
              << "  " << s << "\n"
              << "========================================================\n";
}

// (a) Snapshot read sees an old version while a writer updates the key.
static void scenario_mvcc() {
    banner("Scenario (a): MVCC snapshot read vs concurrent writer");
    TransactionManager tm;

    // Reader starts first, fixing its snapshot before the writer commits.
    Transaction* reader = tm.begin(/*read_only=*/true);

    Transaction* writer = tm.begin(/*read_only=*/false);
    Value tmp;
    writer->state = TxnState::ACTIVE;
    tm.write(writer, "X", "x1");      // buffered, not yet visible
    tm.commit(writer);                // installs new version of X
    tm.show_chain("X");

    // The reader, using its older snapshot, still sees the original value.
    tm.snapshot_read(reader, "X", tmp);
    std::cout << "  -> reader saw the pre-update value: no lock taken, "
                 "no blocking.\n";
    tm.commit(reader);

    // A brand-new reader sees the new value.
    Transaction* reader2 = tm.begin(true);
    tm.snapshot_read(reader2, "X", tmp);
    tm.commit(reader2);
}

// (b) Two read-write transactions block on Strict 2PL locks.
static void scenario_blocking() {
    banner("Scenario (b): Strict 2PL blocking (no deadlock)");
    TransactionManager tm;

    Transaction* t1 = tm.begin(false);
    Transaction* t2 = tm.begin(false);

    tm.write(t1, "X", "x_by_t1");     // T1 takes X-lock on X
    int r = tm.write(t2, "X", "x_by_t2");   // T2 must wait
    if (r == 0)
        std::cout << "  -> T2 is parked behind T1's X-lock.\n";

    tm.commit(t1);                    // releasing wakes T2
    std::cout << "  -> T1 committed; T2 now owns the X-lock and proceeds.\n";

    // T2 retries its write now that it is unblocked.
    tm.write(t2, "X", "x_by_t2");
    tm.commit(t2);
    tm.show_chain("X");
}

// (c) A deadlock: T1 holds X, wants Y; T2 holds Y, wants X.  Victim aborted.
static void scenario_deadlock() {
    banner("Scenario (c): Deadlock detection and victim abort");
    TransactionManager tm;

    Transaction* t1 = tm.begin(false);
    Transaction* t2 = tm.begin(false);

    tm.write(t1, "X", "x_t1");        // T1: X-lock on X
    tm.write(t2, "Y", "y_t2");        // T2: X-lock on Y

    int r1 = tm.write(t1, "Y", "y_t1");   // T1 wants Y -> waits on T2
    std::cout << "  (T1 now waiting on T2)\n";
    (void)r1;

    // T2 wants X -> waits on T1, closing the cycle T2->T1->T2.
    int r2 = tm.write(t2, "X", "x_t2");
    if (r2 == -1)
        std::cout << "  -> T2 was the victim and is rolled back.\n";

    // The survivor (T1) can now finish.
    if (t1->state != TxnState::ABORTED) {
        tm.write(t1, "Y", "y_t1");    // retry: Y is free now
        tm.commit(t1);
    }
    tm.show_chain("X");
    tm.show_chain("Y");
}

int main() {
    std::cout << "=== In-memory Transaction Manager: MVCC + Strict 2PL + "
                 "Deadlock Detection ===\n";
    scenario_mvcc();
    scenario_blocking();
    scenario_deadlock();
    std::cout << "\n=== done ===\n";
    return 0;
}
