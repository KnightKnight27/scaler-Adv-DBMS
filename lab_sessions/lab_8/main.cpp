// Lab 8 - In-memory transaction manager (MVCC + Strict 2PL + deadlock detection)
// MVCC handles reads via per-key version chains and a snapshot taken on start().
// Strict 2PL handles writes via shared/exclusive row locks held until commit.
// A DFS over the waits-for graph detects deadlocks and aborts the youngest tx.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

using TxnId  = uint64_t;
using Stamp  = uint64_t;
using RowKey = string;

enum class TxnState { Running, Finished, Killed };
enum class LockKind { Read, Write };

struct TxnFailure : runtime_error {
    explicit TxnFailure(const string& why) : runtime_error(why) {}
};

struct TxnRecord {
    TxnId    tid;
    Stamp    snap;
    Stamp    commitStamp = 0;
    TxnState state       = TxnState::Running;
    bool     shrinking   = false;
};

struct RowVersion {
    string data;
    TxnId  creator;
    TxnId  invalidator;
    bool   deleted;
};

struct LockOwner {
    TxnId    owner;
    LockKind kind;
};

class TxManager {
    // ── transaction table ──────────────────────────────────────────────────────
    mutex                            txnMu;
    unordered_map<TxnId,TxnRecord>   txnTable;
    atomic<TxnId>                    nextId{1};
    atomic<Stamp>                    globalStamp{1};

    // ── MVCC store ─────────────────────────────────────────────────────────────
    mutex                                        storeMu;
    unordered_map<RowKey,list<RowVersion>>        store;

    // ── lock table ─────────────────────────────────────────────────────────────
    struct LockEntry {
        vector<LockOwner>   owners;
        vector<pair<TxnId,LockKind>> waiters;
        mutex               mu;
        condition_variable  cv;
    };
    mutex                              lockTableMu;
    unordered_map<RowKey,LockEntry>    lockTable;

    // ── waits-for graph ────────────────────────────────────────────────────────
    mutex                                          wfMu;
    unordered_map<TxnId,unordered_set<TxnId>>      waitsFor;

    // ── helpers ────────────────────────────────────────────────────────────────
    bool finishedBefore(TxnId tid, Stamp horizon) {
        lock_guard lk(txnMu);
        auto it = txnTable.find(tid);
        if (it == txnTable.end()) return false;
        return it->second.state == TxnState::Finished &&
               it->second.commitStamp < horizon;
    }

    bool isVisible(const RowVersion& v, TxnId tx) {
        lock_guard lk(txnMu);
        auto& rec = txnTable.at(tx);

        // creator must be us or a committed tx whose commitStamp <= our snap
        bool creatorOk = (v.creator == tx) ||
            ([&]{
                auto it = txnTable.find(v.creator);
                return it != txnTable.end() &&
                       it->second.state == TxnState::Finished &&
                       it->second.commitStamp <= rec.snap;
            }());
        if (!creatorOk) return false;

        // no invalidator, or invalidator is in the future / aborted
        if (v.invalidator == 0) return true;
        if (v.invalidator == tx) return false;   // we deleted it
        auto it = txnTable.find(v.invalidator);
        if (it == txnTable.end()) return true;
        if (it->second.state == TxnState::Killed) return true;
        return it->second.commitStamp > rec.snap;
    }

    void ensureWritable(TxnId tx, const list<RowVersion>& chain) {
        for (const RowVersion& v : chain) {
            if (v.creator == tx) continue;
            if (v.invalidator != 0 && v.invalidator != tx) {
                lock_guard lk(txnMu);
                auto it = txnTable.find(v.invalidator);
                if (it != txnTable.end() && it->second.state == TxnState::Running)
                    throw TxnFailure("write-write conflict: row already being modified");
            }
        }
    }

    // DFS cycle detection on waits-for graph
    bool hasCycle(TxnId start) {
        unordered_set<TxnId> visited, onStack;
        function<bool(TxnId)> dfs = [&](TxnId node) -> bool {
            visited.insert(node); onStack.insert(node);
            auto it = waitsFor.find(node);
            if (it != waitsFor.end())
                for (TxnId nb : it->second) {
                    if (!visited.count(nb) && dfs(nb)) return true;
                    if (onStack.count(nb)) return true;
                }
            onStack.erase(node);
            return false;
        };
        return dfs(start);
    }

    void lockRow(TxnId tx, const RowKey& k, LockKind kind) {
        {
            lock_guard tlk(txnMu);
            if (txnTable.at(tx).shrinking)
                throw TxnFailure("2PL violation: lock acquire after shrinking phase");
        }

        unique_lock ltk(lockTableMu);
        auto& entry = lockTable[k];
        ltk.unlock();

        unique_lock elk(entry.mu);

        // Already hold a sufficient lock?
        for (auto& o : entry.owners) {
            if (o.owner == tx) {
                if (kind == LockKind::Read || o.kind == LockKind::Write) return;
                // upgrade: will re-check below
            }
        }

        while (true) {
            bool conflict = false;
            unordered_set<TxnId> blockers;
            for (auto& o : entry.owners) {
                if (o.owner == tx) continue;
                if (kind == LockKind::Write || o.kind == LockKind::Write) {
                    conflict = true; blockers.insert(o.owner);
                }
            }

            if (!conflict) {
                // Remove any existing read lock we hold before upgrading
                entry.owners.erase(
                    remove_if(entry.owners.begin(), entry.owners.end(),
                              [&](auto& o){ return o.owner == tx; }),
                    entry.owners.end());
                entry.owners.push_back({tx, kind});
                lock_guard wlk(wfMu); waitsFor.erase(tx);
                return;
            }

            {
                lock_guard wlk(wfMu);
                waitsFor[tx] = blockers;
                if (hasCycle(tx)) {
                    waitsFor.erase(tx);
                    throw TxnFailure("deadlock detected — aborting tx " + to_string(tx));
                }
            }
            entry.cv.wait(elk);
        }
    }

    void releaseAll(TxnId tx) {
        {
            lock_guard tlk(txnMu);
            txnTable[tx].shrinking = true;
        }
        lock_guard ltk(lockTableMu);
        for (auto& [k, entry] : lockTable) {
            unique_lock elk(entry.mu);
            entry.owners.erase(
                remove_if(entry.owners.begin(), entry.owners.end(),
                          [&](auto& o){ return o.owner == tx; }),
                entry.owners.end());
            entry.cv.notify_all();
        }
        lock_guard wlk(wfMu); waitsFor.erase(tx);
    }

public:
    TxnId start() {
        lock_guard lk(txnMu);
        TxnId id = nextId++;
        txnTable[id] = TxnRecord{id, globalStamp.load(), 0, TxnState::Running, false};
        return id;
    }

    optional<string> fetch(TxnId tx, const RowKey& k) {
        lockRow(tx, k, LockKind::Read);
        lock_guard lk(storeMu);
        auto it = store.find(k);
        if (it == store.end()) return nullopt;
        for (const RowVersion& v : it->second)
            if (isVisible(v, tx)) return v.deleted ? nullopt : optional<string>{v.data};
        return nullopt;
    }

    void put(TxnId tx, const RowKey& k, const string& data) {
        lockRow(tx, k, LockKind::Write);
        lock_guard lk(storeMu);
        auto& chain = store[k];
        ensureWritable(tx, chain);
        for (RowVersion& v : chain)
            if (isVisible(v, tx) && v.invalidator == 0) { v.invalidator = tx; break; }
        chain.push_front({data, tx, 0, false});
    }

    void erase(TxnId tx, const RowKey& k) {
        lockRow(tx, k, LockKind::Write);
        lock_guard lk(storeMu);
        auto it = store.find(k);
        if (it == store.end()) return;
        ensureWritable(tx, it->second);
        for (RowVersion& v : it->second)
            if (isVisible(v, tx) && v.invalidator == 0) {
                v.invalidator = tx;
                it->second.push_front({"", tx, 0, true});
                return;
            }
    }

    void commit(TxnId tx) {
        {
            lock_guard lk(txnMu);
            txnTable[tx].state       = TxnState::Finished;
            txnTable[tx].commitStamp = ++globalStamp;
        }
        releaseAll(tx);
        cout << "[TX " << tx << "] COMMITTED\n";
    }

    void abort(TxnId tx) {
        {
            lock_guard lk(txnMu);
            txnTable[tx].state = TxnState::Killed;
        }
        releaseAll(tx);
        cout << "[TX " << tx << "] ABORTED\n";
    }

    // MVCC vacuum: prune versions no longer visible to any active transaction
    size_t vacuum() {
        Stamp horizon;
        {
            lock_guard lk(txnMu);
            horizon = globalStamp.load();
            for (auto& [_, t] : txnTable)
                if (t.state == TxnState::Running && t.snap < horizon) horizon = t.snap;
        }
        size_t pruned = 0;
        lock_guard lk(storeMu);
        for (auto& [_, chain] : store) {
            for (auto it = chain.begin(); it != chain.end();) {
                bool dead = it->invalidator != 0 && finishedBefore(it->invalidator, horizon);
                it = dead ? (++pruned, chain.erase(it)) : next(it);
            }
        }
        return pruned;
    }
};

// ─── Demo scenarios ───────────────────────────────────────────────────────────

void show(const optional<string>& v, TxnId tx, const RowKey& k) {
    cout << "  [TX " << tx << "] READ " << k << " = " << (v ? *v : "<not visible>") << "\n";
}

int main() {
    TxManager tm;

    // Scenario 1: MVCC snapshot isolation
    cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxnId t1 = tm.start(); tm.put(t1, "balance", "1000"); tm.commit(t1);
        TxnId t2 = tm.start();
        TxnId t3 = tm.start(); tm.put(t3, "balance", "2000"); tm.commit(t3);
        show(tm.fetch(t2, "balance"), t2, "balance");  // expects 1000 (t3 committed after t2 started)
        tm.commit(t2);
    }

    // Scenario 2: Concurrent shared locks
    cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxnId t4 = tm.start(), t5 = tm.start();
        show(tm.fetch(t4, "balance"), t4, "balance");
        show(tm.fetch(t5, "balance"), t5, "balance");
        tm.commit(t4); tm.commit(t5);
    }

    // Scenario 3: Exclusive lock blocks reader
    cout << "\n=== Scenario 3: Exclusive Lock Blocks Reader ===\n";
    {
        TxnId t6 = tm.start();
        tm.put(t6, "balance", "3000");

        thread reader([&]{
            TxnId t7 = tm.start();
            cout << "  [TX " << t7 << "] waiting for shared lock...\n";
            show(tm.fetch(t7, "balance"), t7, "balance");
            tm.commit(t7);
        });

        this_thread::sleep_for(chrono::milliseconds(50));
        tm.commit(t6);
        reader.join();
    }

    // Scenario 4: Deadlock detection
    cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxnId ta = tm.start(); tm.put(ta, "X", "x_val"); tm.commit(ta);
        TxnId tb = tm.start(); tm.put(tb, "Y", "y_val"); tm.commit(tb);

        TxnId t8 = tm.start(), t9 = tm.start();
        tm.fetch(t8, "X");  // t8 holds shared lock on X
        tm.fetch(t9, "Y");  // t9 holds shared lock on Y

        // t8 tries to write Y (held by t9), t9 tries to write X (held by t8) → deadlock
        thread th([&]{
            try   { tm.put(t8, "Y", "from_t8"); tm.commit(t8); }
            catch (TxnFailure& e) { cout << "  " << e.what() << "\n"; tm.abort(t8); }
        });

        this_thread::sleep_for(chrono::milliseconds(20));
        try   { tm.put(t9, "X", "from_t9"); tm.commit(t9); }
        catch (TxnFailure& e) { cout << "  " << e.what() << "\n"; tm.abort(t9); }

        th.join();
    }

    // Scenario 5: Vacuum
    cout << "\n=== Scenario 5: MVCC Vacuum ===\n";
    {
        size_t pruned = tm.vacuum();
        cout << "  Vacuumed " << pruned << " stale version(s)\n";
    }

    cout << "\nAll scenarios complete.\n";
}
