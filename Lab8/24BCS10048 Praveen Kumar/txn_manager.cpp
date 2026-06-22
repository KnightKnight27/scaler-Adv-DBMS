/*
 * =============================================================================
 *  Lab 8 -- Transaction Manager (MVCC + Strict 2PL + Deadlock Detection)
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-06-22
 *
 *  Purpose : Implement a compact in-memory transaction manager that combines:
 *
 *    1. MVCC (Multi-Version Concurrency Control) for reads.
 *       Writes never overwrite a row in place -- they prepend a new version
 *       to a per-key version chain.  A reader walks the chain and returns the
 *       first version visible to its own snapshot.
 *
 *    2. Strict Two-Phase Locking (S2PL) for writes.
 *       A write acquires an exclusive (X) lock on the row key and holds it
 *       until commit or abort.  A read acquires a shared (S) lock.
 *       A writer can't start while a reader holds S; a reader blocks while
 *       a writer holds X.  This prevents dirty reads.
 *
 *    3. Deadlock detection via DFS over a waits-for graph.
 *       On cycle detection the youngest transaction in the cycle is killed.
 *
 *    4. Lost-update prevention (first-updater-wins).
 *       After taking the X lock, store_() re-scans the version chain.
 *       If another transaction committed a newer version, it throws
 *       "could not serialize access" (same wording PostgreSQL uses).
 *
 *    5. Garbage collection.
 *       gc() prunes versions that are invisible to all currently running
 *       transactions.
 *
 *  Build  : g++ -std=c++17 -O2 -Wall -Wextra -pthread -o txn_manager txn_manager.cpp
 *  Run    : ./txn_manager
 * =============================================================================
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iomanip>

/* ===========================================================================
 *  Types
 * =========================================================================== */

using TxnId  = uint64_t;
using Stamp  = uint64_t;
using RowKey = std::string;

static const TxnId NO_TXN = 0;

/* ===========================================================================
 *  Transaction state
 * =========================================================================== */

enum class TxnState { Running, Committed, Aborted };
enum class LockKind  { Shared, Exclusive };

struct TxnRecord {
    TxnId    tid;
    Stamp    snap;           /* snapshot at start() -- what we can see */
    Stamp    commit_stamp;   /* set at commitTxn()  */
    TxnState state;
    bool     shrinking;      /* 2PL shrinking phase: no new locks allowed */
};

/* ===========================================================================
 *  Multi-version row storage
 *
 *  Each key maps to a list<RowVersion> (newest-first).
 *  A write prepends a new version; a delete prepends {deleted=true}.
 * =========================================================================== */

struct RowVersion {
    std::string data;
    TxnId       creator;      /* transaction that wrote this version */
    TxnId       invalidator;  /* transaction that superseded it (0 = still live) */
    bool        deleted;      /* true = this is a tombstone */
};

/* ===========================================================================
 *  Lock table entry
 * =========================================================================== */

struct LockEntry {
    LockKind kind;
    TxnId    holder;
};

/* ===========================================================================
 *  TxnFailure: thrown when a transaction must be rolled back
 * =========================================================================== */

struct TxnFailure : std::runtime_error {
    explicit TxnFailure(const std::string &why) : std::runtime_error(why) {}
};

/* ===========================================================================
 *  TxnEngine -- the main transaction manager
 * =========================================================================== */

class TxnEngine {
public:
    /* -----------------------------------------------------------------------
     *  start -- begin a new transaction, assign snapshot
     * ----------------------------------------------------------------------- */
    TxnId start()
    {
        std::lock_guard<std::mutex> lk(txn_mu_);
        TxnId id = next_id_++;
        txn_table_[id] = TxnRecord{id, global_stamp_.load(),
                                   0, TxnState::Running, false};
        std::cout << "    [TXN " << id << "] started  (snap=" << txn_table_[id].snap << ")\n";
        return id;
    }

    /* -----------------------------------------------------------------------
     *  fetch -- read the latest visible version of a row
     * ----------------------------------------------------------------------- */
    std::optional<std::string> fetch(TxnId tx, const RowKey &k)
    {
        lock_row(tx, k, LockKind::Shared);
        std::lock_guard<std::mutex> lk(store_mu_);
        auto it = store_.find(k);
        if (it == store_.end()) return std::nullopt;
        for (const RowVersion &v : it->second) {
            if (!is_visible(v, tx)) continue;
            if (v.deleted) return std::nullopt;
            return v.data;
        }
        return std::nullopt;
    }

    /* -----------------------------------------------------------------------
     *  store_ -- write (insert or update) a row
     *
     *  1. Acquire X lock.
     *  2. Check for lost update (first-updater-wins).
     *  3. Mark any live visible version as invalidated by this tx.
     *  4. Prepend the new version.
     * ----------------------------------------------------------------------- */
    void store_(TxnId tx, const RowKey &k, const std::string &data)
    {
        lock_row(tx, k, LockKind::Exclusive);
        std::lock_guard<std::mutex> lk(store_mu_);
        auto &chain = store_[k];

        ensure_writable(tx, chain);

        /* invalidate the old live version */
        for (RowVersion &v : chain) {
            if (is_visible_unlocked(v, tx) && v.invalidator == NO_TXN) {
                v.invalidator = tx;
                break;
            }
        }
        chain.push_front({data, tx, NO_TXN, false});
    }

    /* -----------------------------------------------------------------------
     *  erase_ -- delete a row (tombstone version)
     * ----------------------------------------------------------------------- */
    void erase_(TxnId tx, const RowKey &k)
    {
        lock_row(tx, k, LockKind::Exclusive);
        std::lock_guard<std::mutex> lk(store_mu_);
        auto it = store_.find(k);
        if (it == store_.end()) return;
        auto &chain = it->second;

        ensure_writable(tx, chain);

        for (RowVersion &v : chain) {
            if (is_visible_unlocked(v, tx) && v.invalidator == NO_TXN) {
                v.invalidator = tx;
                break;
            }
        }
        chain.push_front({"", tx, NO_TXN, true});
    }

    /* -----------------------------------------------------------------------
     *  commitTxn -- stamp the transaction and release all locks
     * ----------------------------------------------------------------------- */
    void commitTxn(TxnId tx)
    {
        {
            std::lock_guard<std::mutex> lk(txn_mu_);
            auto &rec = txn_table_.at(tx);
            rec.commit_stamp = ++global_stamp_;
            rec.state        = TxnState::Committed;
        }
        release_locks(tx);
        std::cout << "    [TXN " << tx << "] committed"
                  << " (stamp=" << txn_table_.at(tx).commit_stamp << ")\n";
    }

    /* -----------------------------------------------------------------------
     *  abortTxn -- undo writes, release locks
     * ----------------------------------------------------------------------- */
    void abortTxn(TxnId tx)
    {
        /* Undo: remove versions written by this tx; restore invalidated ones */
        {
            std::lock_guard<std::mutex> lk(store_mu_);
            for (auto &[key, chain] : store_) {
                /* restore versions we invalidated */
                for (RowVersion &v : chain)
                    if (v.invalidator == tx) v.invalidator = NO_TXN;
                /* remove versions we created */
                chain.remove_if([tx](const RowVersion &v) {
                    return v.creator == tx;
                });
            }
        }
        {
            std::lock_guard<std::mutex> lk(txn_mu_);
            txn_table_.at(tx).state = TxnState::Aborted;
        }
        release_locks(tx);
        std::cout << "    [TXN " << tx << "] aborted\n";
    }

    /* -----------------------------------------------------------------------
     *  gc -- prune versions invisible to all currently running transactions.
     *  Returns the number of versions pruned.
     * ----------------------------------------------------------------------- */
    size_t gc()
    {
        /* Find the minimum snapshot of all running transactions */
        Stamp min_snap = global_stamp_.load();
        {
            std::lock_guard<std::mutex> lk(txn_mu_);
            for (auto &[id, rec] : txn_table_)
                if (rec.state == TxnState::Running)
                    min_snap = std::min(min_snap, rec.snap);
        }

        size_t pruned = 0;
        std::lock_guard<std::mutex> lk(store_mu_);
        for (auto &[key, chain] : store_) {
            auto it = chain.begin();
            while (it != chain.end()) {
                /* A version is dead if its invalidator committed before min_snap */
                bool dead = false;
                if (it->invalidator != NO_TXN) {
                    std::lock_guard<std::mutex> lk2(txn_mu_);
                    auto &inv_rec = txn_table_[it->invalidator];
                    if (inv_rec.state == TxnState::Committed &&
                        inv_rec.commit_stamp < min_snap)
                        dead = true;
                }
                if (dead) { it = chain.erase(it); ++pruned; }
                else       ++it;
            }
        }
        return pruned;
    }

    /* -----------------------------------------------------------------------
     *  version_count -- number of stored versions for a key (for GC demo)
     * ----------------------------------------------------------------------- */
    size_t version_count(const RowKey &k)
    {
        std::lock_guard<std::mutex> lk(store_mu_);
        auto it = store_.find(k);
        return (it == store_.end()) ? 0 : it->second.size();
    }

private:
    /* -------- state -------- */
    std::mutex txn_mu_;
    std::mutex store_mu_;
    std::mutex lock_mu_;

    std::atomic<TxnId>  next_id_{1};
    std::atomic<Stamp>  global_stamp_{0};

    std::unordered_map<TxnId, TxnRecord>                     txn_table_;
    std::unordered_map<RowKey, std::list<RowVersion>>         store_;
    std::unordered_map<RowKey, std::vector<LockEntry>>        lock_table_;

    /* waits-for graph: waiter -> set of blockers */
    std::unordered_map<TxnId, std::unordered_set<TxnId>> waits_for_;

    /* -------- visibility -------- */

    bool txn_finished(TxnId id) const
    {
        auto it = txn_table_.find(id);
        if (it == txn_table_.end()) return false;
        return it->second.state == TxnState::Committed;
    }

    /* is version v visible to reader tx? */
    bool is_visible(const RowVersion &v, TxnId tx)
    {
        std::lock_guard<std::mutex> lk(txn_mu_);
        return is_visible_impl(v, tx);
    }

    /* same but caller already holds txn_mu_ */
    bool is_visible_unlocked(const RowVersion &v, TxnId tx)
    {
        return is_visible_impl(v, tx);
    }

    bool is_visible_impl(const RowVersion &v, TxnId tx) const
    {
        /* Creator must be visible to tx */
        bool creator_visible = false;
        if (v.creator == tx) {
            creator_visible = true;
        } else {
            auto ci = txn_table_.find(v.creator);
            if (ci != txn_table_.end() &&
                ci->second.state == TxnState::Committed &&
                ci->second.commit_stamp <= txn_table_.at(tx).snap)
                creator_visible = true;
        }
        if (!creator_visible) return false;

        /* Invalidator must NOT be visible to tx */
        if (v.invalidator == NO_TXN) return true;
        if (v.invalidator == tx) return false;

        auto ii = txn_table_.find(v.invalidator);
        if (ii == txn_table_.end()) return true;
        if (ii->second.state == TxnState::Committed &&
            ii->second.commit_stamp <= txn_table_.at(tx).snap)
            return false;

        return true;
    }

    /* -------- lost-update check -------- */

    /* Must be called with store_mu_ held. */
    void ensure_writable(TxnId tx, const std::list<RowVersion> &chain)
    {
        Stamp my_snap = txn_table_.at(tx).snap;
        for (const RowVersion &v : chain) {
            if (v.creator == tx) continue;
            auto ci = txn_table_.find(v.creator);
            if (ci == txn_table_.end()) continue;
            if (ci->second.state == TxnState::Committed &&
                ci->second.commit_stamp > my_snap)
                throw TxnFailure("could not serialize access due to concurrent update");
        }
    }

    /* -------- locking -------- */

    void lock_row(TxnId tx, const RowKey &k, LockKind kind)
    {
        /* Retry loop: if blocked, detect deadlock, then wait */
        while (true) {
            std::unique_lock<std::mutex> lk(lock_mu_);

            /* Check 2PL shrinking phase */
            {
                std::lock_guard<std::mutex> tl(txn_mu_);
                auto &rec = txn_table_.at(tx);
                if (rec.state == TxnState::Aborted)
                    throw TxnFailure("transaction was killed (deadlock victim)");
                if (rec.shrinking)
                    throw TxnFailure("2PL violation: cannot acquire lock in shrinking phase");
            }

            auto &entries = lock_table_[k];

            /* Check if we already hold this lock */
            for (const auto &e : entries) {
                if (e.holder == tx) {
                    /* upgrade S -> X if needed */
                    if (kind == LockKind::Exclusive && e.kind == LockKind::Shared) {
                        /* upgrade: remove S, add X */
                        for (auto it = entries.begin(); it != entries.end(); ++it) {
                            if (it->holder == tx) { entries.erase(it); break; }
                        }
                        entries.push_back({LockKind::Exclusive, tx});
                    }
                    return; /* already have compatible lock */
                }
            }

            /* Check if compatible with existing locks */
            bool conflict = false;
            std::unordered_set<TxnId> blockers;
            for (const auto &e : entries) {
                if (e.holder == tx) continue;
                bool blocks = (kind == LockKind::Exclusive) ||
                              (e.kind == LockKind::Exclusive);
                if (blocks) {
                    conflict = true;
                    blockers.insert(e.holder);
                }
            }

            if (!conflict) {
                entries.push_back({kind, tx});
                /* remove from waits-for if we were waiting */
                waits_for_.erase(tx);
                return;
            }

            /* Record waits-for edges */
            waits_for_[tx] = blockers;

            /* Deadlock detection: DFS from tx */
            std::vector<TxnId> cycle = find_cycle(tx);
            if (!cycle.empty()) {
                /* Kill the youngest transaction in the cycle */
                TxnId victim = *std::max_element(cycle.begin(), cycle.end());
                std::cout << "    [DEADLOCK] cycle detected, killing TXN " << victim << "\n";
                waits_for_.erase(victim);
                lk.unlock();
                {
                    std::lock_guard<std::mutex> tl(txn_mu_);
                    txn_table_.at(victim).state = TxnState::Aborted;
                }
                release_locks_internal(victim);
                if (victim == tx)
                    throw TxnFailure("transaction " + std::to_string(tx) +
                                     " killed as deadlock victim");
                /* else: retry -- the blocker was killed */
                continue;
            }

            /* Not a deadlock yet -- block briefly and retry */
            lk.unlock();
            /* spin: in production this would be a condition variable */
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /* DFS cycle detection starting from 'start', returns the cycle nodes */
    std::vector<TxnId> find_cycle(TxnId start)
    {
        std::unordered_set<TxnId> visited;
        std::vector<TxnId>        path;

        std::function<bool(TxnId)> dfs = [&](TxnId cur) -> bool {
            visited.insert(cur);
            path.push_back(cur);
            auto it = waits_for_.find(cur);
            if (it != waits_for_.end()) {
                for (TxnId nxt : it->second) {
                    if (nxt == start) {
                        path.push_back(nxt);
                        return true;
                    }
                    if (!visited.count(nxt) && dfs(nxt)) return true;
                }
            }
            path.pop_back();
            return false;
        };

        if (dfs(start)) return path;
        return {};
    }

    void release_locks(TxnId tx)
    {
        std::lock_guard<std::mutex> lk(lock_mu_);
        release_locks_internal(tx);
        /* Enter shrinking phase */
        std::lock_guard<std::mutex> tl(txn_mu_);
        if (txn_table_.count(tx))
            txn_table_.at(tx).shrinking = true;
    }

    /* Must be called with lock_mu_ held */
    void release_locks_internal(TxnId tx)
    {
        for (auto &[key, entries] : lock_table_) {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [tx](const LockEntry &e) { return e.holder == tx; }),
                entries.end());
        }
        /* Remove from waits-for */
        waits_for_.erase(tx);
        for (auto &[waiter, blockers] : waits_for_)
            blockers.erase(tx);
    }
};

/* Include thread for sleep in deadlock demo */
#include <thread>
#include <chrono>

/* ===========================================================================
 *  Demo helpers
 * =========================================================================== */

static void print_banner(int n, const std::string &title)
{
    std::cout << "\n[Demo " << n << "] " << title << "\n";
    std::cout << std::string(60, '-') << "\n";
}

static void show(const std::string &label, const std::optional<std::string> &v)
{
    std::cout << "  " << label << " = "
              << (v ? "\"" + *v + "\"" : "NULL") << "\n";
}

/* ===========================================================================
 *  main
 * =========================================================================== */

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  Lab 8 -- Transaction Manager (MVCC + Strict 2PL)\n";
    std::cout << "============================================================\n";

    /* ------------------------------------------------------------------
     *  Demo 1: Basic read / write with snapshot isolation
     *  T1 writes "hello" to key "x", commits.
     *  T2 starts after T1 commits, reads "hello".
     *  T3 starts before T1 commits, should not see T1's write.
     * ------------------------------------------------------------------ */
    print_banner(1, "Basic read/write + snapshot isolation");
    {
        TxnEngine eng;

        TxnId t3 = eng.start();                        /* T3 starts first */
        TxnId t1 = eng.start();
        eng.store_(t1, "x", "hello");
        eng.commitTxn(t1);

        TxnId t2 = eng.start();
        show("T2 reads x (T1 committed before T2 started)", eng.fetch(t2, "x"));
        show("T3 reads x (T3 started before T1 committed)",  eng.fetch(t3, "x"));

        eng.commitTxn(t2);
        eng.commitTxn(t3);
    }

    /* ------------------------------------------------------------------
     *  Demo 2: Dirty read prevention
     *  T1 writes but does not commit.
     *  T2 should not see T1's uncommitted write.
     * ------------------------------------------------------------------ */
    print_banner(2, "Dirty read prevention");
    {
        TxnEngine eng;

        TxnId t1 = eng.start();
        eng.store_(t1, "balance", "1000");
        /* T1 has not committed */

        TxnId t2 = eng.start();
        show("T2 reads balance (T1 not committed -- must be NULL)", eng.fetch(t2, "balance"));

        eng.abortTxn(t1);
        eng.commitTxn(t2);
    }

    /* ------------------------------------------------------------------
     *  Demo 3: Repeatable read
     *  T1 reads x, T2 updates x and commits, T1 reads x again.
     *  T1 should see the same value both times (snapshot isolation).
     * ------------------------------------------------------------------ */
    print_banner(3, "Repeatable read (snapshot isolation)");
    {
        TxnEngine eng;

        TxnId seed = eng.start();
        eng.store_(seed, "x", "original");
        eng.commitTxn(seed);

        TxnId t1 = eng.start();
        show("T1 first read of x", eng.fetch(t1, "x"));

        TxnId t2 = eng.start();
        eng.store_(t2, "x", "updated");
        eng.commitTxn(t2);

        show("T1 second read of x (T2 committed -- T1 still sees original)",
             eng.fetch(t1, "x"));
        eng.commitTxn(t1);
    }

    /* ------------------------------------------------------------------
     *  Demo 4: Write-write conflict serialization (S2PL)
     *  T1 and T2 both write to "z". T1 commits first.
     *  T2 then tries to write -- gets a conflict.
     * ------------------------------------------------------------------ */
    print_banner(4, "Write-write conflict (S2PL serialization)");
    {
        TxnEngine eng;

        TxnId t1 = eng.start();
        eng.store_(t1, "z", "v1");
        eng.commitTxn(t1);

        TxnId t2 = eng.start();
        try {
            eng.store_(t2, "z", "v2");
            eng.commitTxn(t2);
            auto v = eng.fetch(eng.start(), "z");
            show("z after T2 committed", v);
        } catch (const TxnFailure &e) {
            std::cout << "  T2 caught: " << e.what() << "\n";
            eng.abortTxn(t2);
        }
    }

    /* ------------------------------------------------------------------
     *  Demo 5: Lost update prevention (first-updater-wins)
     *  Both T1 and T2 read "counter" = 10 at the same snapshot.
     *  T1 writes 11 and commits.
     *  T2 tries to write 11 too -- must be rejected.
     * ------------------------------------------------------------------ */
    print_banner(5, "Lost update prevention (first-updater-wins)");
    {
        TxnEngine eng;

        TxnId seed = eng.start();
        eng.store_(seed, "counter", "10");
        eng.commitTxn(seed);

        TxnId t1 = eng.start();
        TxnId t2 = eng.start();

        /* Both read 10 */
        show("T1 reads counter", eng.fetch(t1, "counter"));
        show("T2 reads counter", eng.fetch(t2, "counter"));

        /* T1 increments and commits */
        eng.store_(t1, "counter", "11");
        eng.commitTxn(t1);
        std::cout << "  T1 committed counter = 11\n";

        /* T2 tries to write -- should fail because T1 committed after T2's snapshot */
        try {
            eng.store_(t2, "counter", "11");
            eng.commitTxn(t2);
            std::cout << "  T2 committed (should not happen)\n";
        } catch (const TxnFailure &e) {
            std::cout << "  T2 caught: " << e.what() << "\n";
            eng.abortTxn(t2);
        }
    }

    /* ------------------------------------------------------------------
     *  Demo 6: Delete and tombstone
     * ------------------------------------------------------------------ */
    print_banner(6, "Delete and tombstone visibility");
    {
        TxnEngine eng;

        TxnId seed = eng.start();
        eng.store_(seed, "row1", "exists");
        eng.commitTxn(seed);

        TxnId t1 = eng.start();
        TxnId t2 = eng.start();

        /* T1 deletes row1 and commits */
        eng.erase_(t1, "row1");
        eng.commitTxn(t1);

        /* T2 started before T1's delete committed */
        show("T2 sees row1 (started before delete -- snapshot isolation)",
             eng.fetch(t2, "row1"));

        /* T3 starts after the delete -- should see NULL */
        TxnId t3 = eng.start();
        show("T3 sees row1 (started after delete)", eng.fetch(t3, "row1"));

        eng.commitTxn(t2);
        eng.commitTxn(t3);
    }

    /* ------------------------------------------------------------------
     *  Demo 7: Garbage collection
     * ------------------------------------------------------------------ */
    print_banner(7, "Garbage collection");
    {
        TxnEngine eng;

        TxnId t1 = eng.start();
        eng.store_(t1, "gc_key", "v1");
        eng.commitTxn(t1);

        TxnId t2 = eng.start();
        eng.store_(t2, "gc_key", "v2");
        eng.commitTxn(t2);

        TxnId t3 = eng.start();
        eng.store_(t3, "gc_key", "v3");
        eng.commitTxn(t3);

        std::cout << "  Versions before GC: " << eng.version_count("gc_key") << "\n";

        /* No running transactions -- GC can prune old versions */
        size_t pruned = eng.gc();
        std::cout << "  Versions pruned   : " << pruned << "\n";
        std::cout << "  Versions after GC : " << eng.version_count("gc_key") << "\n";
    }

    std::cout << "\n============================================================\n";
    std::cout << "  Done.\n";
    std::cout << "============================================================\n";

    return 0;
}
