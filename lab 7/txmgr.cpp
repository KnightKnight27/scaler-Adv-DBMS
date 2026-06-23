/*
 * Lab 7 — Transaction Manager (MVCC + Strict 2PL + Deadlock Detection)
 * Single-file demo for Advanced DBMS.
 *
 * Viva cheat-sheet:
 *   MVCC  → each write adds a version; reads pick the newest visible one.
 *   2PL   → locks only grow until commit/abort, then all released at once.
 *   Deadlock → build waits-for graph; DFS finds a cycle → abort requester.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>

using TxnId = uint64_t;
using Key   = std::string;

// ---------------------------------------------------------------------------
// Transaction registry
// ---------------------------------------------------------------------------

enum class TxnPhase { Growing, Shrinking };
enum class TxnOutcome { Active, Committed, Aborted };

struct TxnInfo {
    TxnId       id;
    TxnId       read_view;          // snapshot: see commits with id < read_view
    TxnOutcome  outcome = TxnOutcome::Active;
    TxnPhase    phase   = TxnPhase::Growing;
};

namespace txn_table {

std::mutex g_mu;
std::atomic<TxnId> g_next_id{1};
std::unordered_map<TxnId, TxnInfo> g_all;

TxnId start() {
    std::lock_guard lock(g_mu);
    TxnId id = g_next_id++;
    g_all[id] = TxnInfo{id, id, TxnOutcome::Active, TxnPhase::Growing};
    return id;
}

TxnInfo snapshot(TxnId id) {
    std::lock_guard lock(g_mu);
    return g_all.at(id);
}

void mark(TxnId id, TxnOutcome outcome) {
    std::lock_guard lock(g_mu);
    g_all.at(id).outcome = outcome;
}

void enter_shrinking(TxnId id) {
    std::lock_guard lock(g_mu);
    if (g_all.count(id)) g_all.at(id).phase = TxnPhase::Shrinking;
}

bool committed(TxnId id) {
    std::lock_guard lock(g_mu);
    auto it = g_all.find(id);
    return it != g_all.end() && it->second.outcome == TxnOutcome::Committed;
}

}  // namespace txn_table

// ---------------------------------------------------------------------------
// MVCC version store
// ---------------------------------------------------------------------------

struct Version {
    std::string value;
    TxnId       created_by;   // xmin
    TxnId       removed_by;   // xmax (0 = still valid)
};

namespace store {

std::mutex g_mu;
std::unordered_map<Key, std::list<Version>> g_rows;

// Can transaction `reader` see this version at its snapshot?
bool visible(const Version& v, TxnId read_view, TxnId reader) {
    bool created = (v.created_by == reader)
                || (txn_table::committed(v.created_by) && v.created_by < read_view);
    if (!created) return false;

    if (v.removed_by == 0) return true;

    bool gone = (v.removed_by == reader)
             || (txn_table::committed(v.removed_by) && v.removed_by < read_view);
    return !gone;
}

std::optional<std::string> read(const Key& key, TxnId reader) {
    std::lock_guard lock(g_mu);
    TxnId view = txn_table::snapshot(reader).read_view;

    auto row = g_rows.find(key);
    if (row == g_rows.end()) return std::nullopt;

    for (const Version& v : row->second)
        if (visible(v, view, reader)) return v.value;
    return std::nullopt;
}

void insert(const Key& key, const std::string& value, TxnId writer) {
    std::lock_guard lock(g_mu);
    g_rows[key].push_front({value, writer, 0});
}

void update(const Key& key, const std::string& value, TxnId writer) {
    std::lock_guard lock(g_mu);
    TxnId view = txn_table::snapshot(writer).read_view;

    auto row = g_rows.find(key);
    if (row != g_rows.end()) {
        for (Version& v : row->second) {
            if (visible(v, view, writer) && v.removed_by == 0) {
                v.removed_by = writer;
                break;
            }
        }
    }
    g_rows[key].push_front({value, writer, 0});
}

void erase(const Key& key, TxnId writer) {
    std::lock_guard lock(g_mu);
    TxnId view = txn_table::snapshot(writer).read_view;

    auto row = g_rows.find(key);
    if (row == g_rows.end()) return;

    for (Version& v : row->second) {
        if (visible(v, view, writer) && v.removed_by == 0) {
            v.removed_by = writer;
            return;
        }
    }
}

void undo(TxnId failed) {
    std::lock_guard lock(g_mu);
    for (auto& [key, chain] : g_rows) {
        for (Version& v : chain) {
            if (v.created_by == failed) v.removed_by = failed;
            else if (v.removed_by == failed) v.removed_by = 0;
        }
    }
}

}  // namespace store

// ---------------------------------------------------------------------------
// Strict two-phase lock manager + deadlock detection
// ---------------------------------------------------------------------------

enum class LockKind { Read, Write };

struct LockWaiter {
    TxnId    txn;
    LockKind kind;
    bool     ok = false;
};

struct LockQueue {
    std::list<LockWaiter> waiters;
    std::mutex            mu;
    std::condition_variable cv;
};

namespace locks {

std::mutex g_table_mu;
std::unordered_map<Key, LockQueue> g_table;
std::unordered_map<TxnId, std::unordered_set<TxnId>> g_waits_for;

class DeadlockError : public std::runtime_error {
public:
    explicit DeadlockError(TxnId id)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(id)) {}
};

bool cycle_from(TxnId start,
                const std::unordered_map<TxnId, std::unordered_set<TxnId>>& graph) {
    std::unordered_set<TxnId> visited, active;

    std::function<bool(TxnId)> dfs = [&](TxnId node) -> bool {
        visited.insert(node);
        active.insert(node);

        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxnId nxt : it->second) {
                if (!visited.count(nxt) && dfs(nxt)) return true;
                if (active.count(nxt)) return true;
            }
        }

        active.erase(node);
        return false;
    };

    return dfs(start);
}

bool conflicts(LockKind want, LockKind held) {
    return want == LockKind::Write || held == LockKind::Write;
}

LockQueue& queue_for(const Key& key) {
    std::lock_guard lock(g_table_mu);
    return g_table[key];
}

void acquire(const Key& key, TxnId txn, LockKind kind) {
    if (txn_table::snapshot(txn).phase == TxnPhase::Shrinking)
        throw std::runtime_error("2PL violation: lock after shrinking phase");

    LockQueue& q = queue_for(key);
    std::unique_lock lock(q.mu);

    for (const LockWaiter& w : q.waiters) {
        if (w.txn == txn && w.ok) {
            if (kind == LockKind::Read || w.kind == LockKind::Write) return;
        }
    }

    q.waiters.push_back({txn, kind, false});
    LockWaiter* me = &q.waiters.back();

    while (true) {
        bool blocked = false;
        std::unordered_set<TxnId> blockers;

        for (const LockWaiter& earlier : q.waiters) {
            if (&earlier == me) break;
            if (!earlier.ok) continue;
            if (conflicts(kind, earlier.kind) && earlier.txn != txn) {
                blocked = true;
                blockers.insert(earlier.txn);
            }
        }

        if (!blocked) {
            me->ok = true;
            std::lock_guard wf(g_table_mu);
            g_waits_for.erase(txn);
            return;
        }

        {
            std::lock_guard wf(g_table_mu);
            g_waits_for[txn] = blockers;
            if (cycle_from(txn, g_waits_for)) {
                g_waits_for.erase(txn);
                q.waiters.remove_if([&](const LockWaiter& w) {
                    return w.txn == txn && !w.ok;
                });
                throw DeadlockError(txn);
            }
        }

        q.cv.wait(lock);
    }
}

void release_all(TxnId txn) {
    txn_table::enter_shrinking(txn);

    std::vector<LockQueue*> queues;
    {
        std::lock_guard lock(g_table_mu);
        for (auto& [key, q] : g_table) queues.push_back(&q);
        g_waits_for.erase(txn);
    }

    for (LockQueue* q : queues) {
        std::lock_guard lock(q->mu);
        q->waiters.remove_if([&](const LockWaiter& w) { return w.txn == txn; });
        q->cv.notify_all();
    }
}

}  // namespace locks

// ---------------------------------------------------------------------------
// Public API used by demo scenarios
// ---------------------------------------------------------------------------

class TxManager {
public:
    TxnId begin() { return txn_table::start(); }

    std::optional<std::string> read(TxnId txn, const Key& key) {
        locks::acquire(key, txn, LockKind::Read);
        return store::read(key, txn);
    }

    void write(TxnId txn, const Key& key, const std::string& value) {
        locks::acquire(key, txn, LockKind::Write);
        if (store::read(key, txn)) store::update(key, value, txn);
        else store::insert(key, value, txn);
    }

    void insert(TxnId txn, const Key& key, const std::string& value) {
        locks::acquire(key, txn, LockKind::Write);
        store::insert(key, value, txn);
    }

    void update(TxnId txn, const Key& key, const std::string& value) {
        locks::acquire(key, txn, LockKind::Write);
        store::update(key, value, txn);
    }

    void remove(TxnId txn, const Key& key) {
        locks::acquire(key, txn, LockKind::Write);
        store::erase(key, txn);
    }

    // Exposed only for deadlock demo (direct lock without MVCC touch).
    void lock_only(const Key& key, TxnId txn, LockKind kind) {
        locks::acquire(key, txn, kind);
    }

    void commit(TxnId txn) {
        txn_table::mark(txn, TxnOutcome::Committed);
        locks::release_all(txn);
        std::cout << "[TX " << txn << "] COMMITTED\n";
    }

    void abort(TxnId txn) {
        store::undo(txn);
        txn_table::mark(txn, TxnOutcome::Aborted);
        locks::release_all(txn);
        std::cout << "[TX " << txn << "] ABORTED\n";
    }
};

// ---------------------------------------------------------------------------
// Demo driver
// ---------------------------------------------------------------------------

static void show(const std::optional<std::string>& val, TxnId txn, const Key& key) {
    std::cout << "  [TX " << txn << "] READ " << key << " = "
              << (val ? *val : "<not visible>") << '\n';
}

int main() {
    TxManager db;

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxnId t1 = db.begin();
        db.insert(t1, "balance", "1000");
        db.commit(t1);

        TxnId t2 = db.begin();
        TxnId t3 = db.begin();

        db.update(t3, "balance", "2000");
        db.commit(t3);

        show(db.read(t2, "balance"), t2, "balance");  // still 1000
        db.commit(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxnId t4 = db.begin();
        TxnId t5 = db.begin();
        show(db.read(t4, "balance"), t4, "balance");
        show(db.read(t5, "balance"), t5, "balance");
        db.commit(t4);
        db.commit(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxnId t6 = db.begin();
        db.update(t6, "balance", "3000");

        std::thread reader([&] {
            TxnId t7 = db.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            show(db.read(t7, "balance"), t7, "balance");
            db.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        db.commit(t6);
        reader.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxnId ta = db.begin();
        TxnId tb = db.begin();
        db.insert(ta, "A", "val_a");
        db.insert(tb, "B", "val_b");
        db.commit(ta);
        db.commit(tb);

        TxnId t8 = db.begin();
        TxnId t9 = db.begin();

        db.lock_only("A", t8, LockKind::Write);
        db.lock_only("B", t9, LockKind::Write);

        std::thread cross_lock([&] {
            try {
                db.lock_only("B", t8, LockKind::Write);
                db.commit(t8);
            } catch (const locks::DeadlockError& e) {
                std::cout << "  " << e.what() << '\n';
                db.abort(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            db.lock_only("A", t9, LockKind::Write);
            db.commit(t9);
        } catch (const locks::DeadlockError& e) {
            std::cout << "  " << e.what() << '\n';
            db.abort(t9);
        }

        cross_lock.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
