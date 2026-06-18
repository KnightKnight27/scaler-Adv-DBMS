// Lab 8 - 24BCS10404 - Rajveer Bishnoi
// Transaction Manager: MVCC + Strict 2PL + deadlock detection in C++17.
// Writes append new versions; reads walk the per-key chain for their snapshot.
// Exclusive locks held until commit/abort; deadlock detected via DFS waits-for graph.

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

class TxnEngine {
public:
    TxnId start() {
        lock_guard<mutex> lk(txnMu);
        TxnId id = nextId++;
        txnTable[id] = TxnRecord{id, globalStamp.load(), 0, TxnState::Running, false};
        return id;
    }

    optional<string> fetch(TxnId tx, const RowKey& k) {
        lockRow(tx, k, LockKind::Read);
        lock_guard<mutex> lk(storeMu);
        auto it = store.find(k);
        if (it == store.end()) return nullopt;
        for (const RowVersion& v : it->second) {
            if (!isVisible(v, tx)) continue;
            if (v.deleted) return nullopt;
            return v.data;
        }
        return nullopt;
    }

    void store_(TxnId tx, const RowKey& k, const string& data) {
        lockRow(tx, k, LockKind::Write);
        lock_guard<mutex> lk(storeMu);
        auto& chain = store[k];
        ensureWritable(tx, chain);
        for (RowVersion& v : chain) {
            if (isVisible(v, tx) && v.invalidator == 0) {
                v.invalidator = tx;
                break;
            }
        }
        chain.push_front({data, tx, 0, false});
    }

    void erase_(TxnId tx, const RowKey& k) {
        lockRow(tx, k, LockKind::Write);
        lock_guard<mutex> lk(storeMu);
        auto it = store.find(k);
        if (it == store.end()) return;
        ensureWritable(tx, it->second);
        for (RowVersion& v : it->second) {
            if (isVisible(v, tx) && v.invalidator == 0) {
                v.invalidator = tx;
                it->second.push_front({"", tx, 0, true});
                return;
            }
        }
    }

    void commitTxn(TxnId tx) {
        {
            lock_guard<mutex> lk(txnMu);
            txnTable[tx].state       = TxnState::Finished;
            txnTable[tx].commitStamp = ++globalStamp;
            txnTable[tx].shrinking   = true;
        }
        releaseAll(tx);
    }

    void abortTxn(TxnId tx) {
        {
            lock_guard<mutex> lk(txnMu);
            txnTable[tx].state     = TxnState::Killed;
            txnTable[tx].shrinking = true;
        }
        releaseAll(tx);
    }

    size_t gc() {
        Stamp horizon;
        {
            lock_guard<mutex> lk(txnMu);
            horizon = globalStamp.load();
            for (auto& [_, t] : txnTable) {
                if (t.state == TxnState::Running && t.snap < horizon)
                    horizon = t.snap;
            }
        }
        size_t pruned = 0;
        lock_guard<mutex> lk(storeMu);
        for (auto& [_, chain] : store) {
            for (auto it = chain.begin(); it != chain.end();) {
                bool dead = it->invalidator != 0
                            && finishedBefore(it->invalidator, horizon);
                if (dead) { it = chain.erase(it); ++pruned; }
                else      { ++it; }
            }
        }
        return pruned;
    }

    size_t versionCount(const RowKey& k) {
        lock_guard<mutex> lk(storeMu);
        auto it = store.find(k);
        return it == store.end() ? 0 : it->second.size();
    }

private:
    bool finishedBefore(TxnId tx, Stamp ts) {
        lock_guard<mutex> lk(txnMu);
        auto it = txnTable.find(tx);
        if (it == txnTable.end() || it->second.state != TxnState::Finished) return false;
        return it->second.commitStamp <= ts;
    }

    bool isFinished(TxnId tx) {
        lock_guard<mutex> lk(txnMu);
        auto it = txnTable.find(tx);
        return it != txnTable.end() && it->second.state == TxnState::Finished;
    }

    Stamp commitStampOf(TxnId tx) {
        lock_guard<mutex> lk(txnMu);
        auto it = txnTable.find(tx);
        return it == txnTable.end() ? 0 : it->second.commitStamp;
    }

    bool isVisible(const RowVersion& v, TxnId reader) {
        Stamp snap;
        {
            lock_guard<mutex> lk(txnMu);
            snap = txnTable.at(reader).snap;
        }
        bool creatorVisible = (v.creator == reader) || finishedBefore(v.creator, snap);
        if (!creatorVisible) return false;
        if (v.invalidator == 0) return true;
        bool invalidatorVisible = (v.invalidator == reader) || finishedBefore(v.invalidator, snap);
        return !invalidatorVisible;
    }

    void ensureWritable(TxnId tx, const list<RowVersion>& chain) {
        Stamp snap;
        {
            lock_guard<mutex> lk(txnMu);
            snap = txnTable.at(tx).snap;
        }
        for (const RowVersion& v : chain) {
            if (v.creator == tx) continue;
            if (isFinished(v.creator) && commitStampOf(v.creator) > snap)
                throw TxnFailure("could not serialize access: row touched by tx "
                                 + to_string(v.creator));
            if (v.invalidator != 0 && v.invalidator != tx
                && isFinished(v.invalidator) && commitStampOf(v.invalidator) > snap)
                throw TxnFailure("could not serialize access: row touched by tx "
                                 + to_string(v.invalidator));
        }
    }

    void lockRow(TxnId tx, const RowKey& k, LockKind kind) {
        unique_lock<mutex> lk(lockMu);
        while (true) {
            {
                lock_guard<mutex> tlk(txnMu);
                if (txnTable[tx].state == TxnState::Killed)
                    throw TxnFailure("aborted by deadlock detector");
                if (txnTable[tx].shrinking)
                    throw TxnFailure("2PL violation: acquire in shrinking phase");
            }
            auto& owners = lockTable[k];

            bool holdRead = false, holdWrite = false, conflict = false;
            for (LockOwner& h : owners) {
                if (h.owner == tx) {
                    if (h.kind == LockKind::Write) holdWrite = true;
                    else                           holdRead  = true;
                    continue;
                }
                if (kind == LockKind::Write || h.kind == LockKind::Write)
                    conflict = true;
            }

            if (holdWrite) return;
            if (holdRead && kind == LockKind::Read) return;
            if (holdRead && kind == LockKind::Write && owners.size() == 1) {
                owners.front().kind = LockKind::Write;
                return;
            }
            if (!conflict && !holdRead) {
                owners.push_back({tx, kind});
                return;
            }

            for (LockOwner& h : owners)
                if (h.owner != tx) waitGraph[tx].insert(h.owner);

            if (TxnId victim = detectDeadlockVictim(tx); victim != 0) {
                waitGraph.erase(tx);
                if (victim == tx) throw TxnFailure("deadlock: victim " + to_string(tx));
                {
                    lock_guard<mutex> tlk(txnMu);
                    txnTable[victim].state     = TxnState::Killed;
                    txnTable[victim].shrinking = true;
                }
                dropLocks(victim);
                lockCv.notify_all();
                continue;
            }

            lockCv.wait(lk);
            waitGraph.erase(tx);
        }
    }

    void releaseAll(TxnId tx) {
        {
            unique_lock<mutex> lk(lockMu);
            dropLocks(tx);
            waitGraph.erase(tx);
            for (auto& [_, deps] : waitGraph) deps.erase(tx);
        }
        lockCv.notify_all();
    }

    void dropLocks(TxnId tx) {
        for (auto it = lockTable.begin(); it != lockTable.end();) {
            auto& v = it->second;
            v.erase(remove_if(v.begin(), v.end(),
                              [&](const LockOwner& h){ return h.owner == tx; }),
                    v.end());
            if (v.empty()) it = lockTable.erase(it);
            else           ++it;
        }
    }

    TxnId detectDeadlockVictim(TxnId start) {
        unordered_set<TxnId> onStack, done;
        vector<TxnId> path;

        function<bool(TxnId)> dfs = [&](TxnId u) -> bool {
            onStack.insert(u);
            path.push_back(u);
            for (TxnId v : waitGraph[u]) {
                if (onStack.count(v)) { path.push_back(v); return true; }
                if (!done.count(v) && dfs(v)) return true;
            }
            onStack.erase(u);
            path.pop_back();
            done.insert(u);
            return false;
        };

        if (!dfs(start)) return 0;
        TxnId victim = 0;
        for (TxnId t : path) if (t > victim) victim = t;
        return victim;
    }

    atomic<TxnId>                              nextId{1};
    atomic<Stamp>                              globalStamp{0};

    mutex                                      txnMu;
    unordered_map<TxnId, TxnRecord>            txnTable;

    mutex                                      storeMu;
    unordered_map<RowKey, list<RowVersion>>    store;

    mutex                                      lockMu;
    condition_variable                         lockCv;
    unordered_map<RowKey, vector<LockOwner>>   lockTable;
    unordered_map<TxnId, unordered_set<TxnId>> waitGraph;
};

static mutex ioMu;
static void emit(const string& s) {
    lock_guard<mutex> lk(ioMu);
    cout << s << "\n";
}

static void demoSnapshot(TxnEngine& engine) {
    emit("=== 1. snapshot isolation: reader sees pre-write value ===");
    TxnId seed = engine.start();
    engine.store_(seed, "acct", "1000");
    engine.commitTxn(seed);

    TxnId reader = engine.start();
    TxnId writer = engine.start();
    engine.store_(writer, "acct", "2000");
    engine.commitTxn(writer);

    auto v = engine.fetch(reader, "acct");
    emit("  reader (tx " + to_string(reader) + ") sees: " + v.value_or("<none>"));
    engine.commitTxn(reader);
}

static void demoSharedLocks(TxnEngine& engine) {
    emit("=== 2. two readers hold shared locks at the same time ===");
    TxnId a = engine.start();
    TxnId b = engine.start();
    auto va = engine.fetch(a, "acct");
    auto vb = engine.fetch(b, "acct");
    emit("  tx " + to_string(a) + " read: " + va.value_or("<none>"));
    emit("  tx " + to_string(b) + " read: " + vb.value_or("<none>"));
    engine.commitTxn(a);
    engine.commitTxn(b);
}

static void demoBlocking(TxnEngine& engine) {
    emit("=== 3. exclusive lock blocks a reader, but reader stays at SI snapshot ===");
    TxnId writer = engine.start();
    engine.store_(writer, "acct", "3000");

    thread readerThread([&] {
        TxnId r = engine.start();
        emit("  reader (tx " + to_string(r) + ") waiting for shared lock...");
        auto v = engine.fetch(r, "acct");
        emit("  reader (tx " + to_string(r) + ") got: " + v.value_or("<none>"));
        engine.commitTxn(r);
    });

    this_thread::sleep_for(chrono::milliseconds(150));
    engine.commitTxn(writer);
    readerThread.join();
}

static void demoUpgrade(TxnEngine& engine) {
    emit("=== 4. lock upgrade S -> X by sole holder ===");
    TxnId t = engine.start();
    auto v = engine.fetch(t, "acct");
    emit("  tx " + to_string(t) + " read under S lock: " + v.value_or("<none>"));
    engine.store_(t, "acct", "4000");
    emit("  tx " + to_string(t) + " upgraded to X lock and wrote 4000");
    engine.commitTxn(t);
}

static void demoDeadlock(TxnEngine& engine) {
    emit("=== 5. deadlock detection (younger tx aborts) ===");
    TxnId t1 = engine.start();
    TxnId t2 = engine.start();
    engine.store_(t1, "X", "1");
    engine.store_(t2, "Y", "1");

    atomic<int> aborts{0};
    auto run = [&](TxnId tx, const RowKey& other) {
        try {
            engine.store_(tx, other, "2");
            engine.commitTxn(tx);
            emit("  tx " + to_string(tx) + " committed");
        } catch (const TxnFailure& e) {
            engine.abortTxn(tx);
            aborts++;
            emit("  tx " + to_string(tx) + " aborted: " + e.what());
        }
    };
    thread th1(run, t1, "Y");
    thread th2(run, t2, "X");
    th1.join();
    th2.join();
    if (aborts.load() == 0) emit("  (deadlock detector missed the cycle)");
}

static void demoLostUpdate(TxnEngine& engine) {
    emit("=== 6. SI rejects a lost update (first-updater-wins) ===");
    TxnId seed = engine.start();
    engine.store_(seed, "tally", "0");
    engine.commitTxn(seed);

    TxnId a = engine.start();
    TxnId b = engine.start();

    thread th([&] {
        try {
            engine.fetch(a, "tally");
            engine.store_(a, "tally", "1");
            engine.commitTxn(a);
            emit("  tx " + to_string(a) + " committed counter=1");
        } catch (const TxnFailure& e) {
            engine.abortTxn(a);
            emit("  tx " + to_string(a) + " aborted: " + e.what());
        }
    });
    this_thread::sleep_for(chrono::milliseconds(60));
    try {
        engine.fetch(b, "tally");
        engine.store_(b, "tally", "2");
        engine.commitTxn(b);
        emit("  tx " + to_string(b) + " committed counter=2");
    } catch (const TxnFailure& e) {
        engine.abortTxn(b);
        emit("  tx " + to_string(b) + " aborted: " + e.what());
    }
    th.join();
}

static void demoVacuum(TxnEngine& engine) {
    emit("=== 7. vacuum prunes dead versions ===");
    for (int i = 0; i < 5; i++) {
        TxnId t = engine.start();
        engine.store_(t, "gckey", "v" + to_string(i));
        engine.commitTxn(t);
    }
    emit("  vkey chain length before vacuum: " + to_string(engine.versionCount("gckey")));
    size_t pruned = engine.gc();
    emit("  vacuum pruned " + to_string(pruned) + " dead versions (across all keys)");
    emit("  vkey chain length after vacuum:  " + to_string(engine.versionCount("gckey")));
}

int main() {
    TxnEngine engine;
    demoSnapshot(engine);
    demoSharedLocks(engine);
    demoBlocking(engine);
    demoUpgrade(engine);
    demoDeadlock(engine);
    demoLostUpdate(engine);
    demoVacuum(engine);
    return 0;
}
