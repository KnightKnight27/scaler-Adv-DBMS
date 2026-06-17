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

using TxId  = uint64_t;
using Clock = uint64_t;
using Key   = string;

enum class TxStatus { Active, Committed, Aborted };
enum class LockMode { Shared, Exclusive };

struct TxAborted : runtime_error {
    explicit TxAborted(const string& why) : runtime_error(why) {}
};

struct Transaction {
    TxId     id;
    Clock    snapshot;
    Clock    commit_ts = 0;
    TxStatus status    = TxStatus::Active;
    bool     shrinking = false;
};

struct Version {
    string value;
    TxId   xmin;
    TxId   xmax;
    bool   tombstone;
};

struct LockHolder {
    TxId     tx;
    LockMode mode;
};

class TxManager {
public:
    TxId begin() {
        lock_guard<mutex> lk(tx_mu);
        TxId id = next_xid++;
        txs[id] = Transaction{id, commit_clock.load(), 0, TxStatus::Active, false};
        return id;
    }

    optional<string> read(TxId tx, const Key& k) {
        acquire(tx, k, LockMode::Shared);
        lock_guard<mutex> lk(heap_mu);
        auto it = heap.find(k);
        if (it == heap.end()) return nullopt;
        for (const Version& v : it->second) {
            if (!visible(v, tx)) continue;
            if (v.tombstone) return nullopt;
            return v.value;
        }
        return nullopt;
    }

    void write(TxId tx, const Key& k, const string& value) {
        acquire(tx, k, LockMode::Exclusive);
        lock_guard<mutex> lk(heap_mu);
        auto& chain = heap[k];

        check_writable(tx, chain);

        for (Version& v : chain) {
            if (visible(v, tx) && v.xmax == 0) {
                v.xmax = tx;
                break;
            }
        }
        chain.push_front({value, tx, 0, false});
    }

    void remove(TxId tx, const Key& k) {
        acquire(tx, k, LockMode::Exclusive);
        lock_guard<mutex> lk(heap_mu);
        auto it = heap.find(k);
        if (it == heap.end()) return;

        check_writable(tx, it->second);

        for (Version& v : it->second) {
            if (visible(v, tx) && v.xmax == 0) {
                v.xmax = tx;
                it->second.push_front({"", tx, 0, true});
                return;
            }
        }
    }

    void commit(TxId tx) {
        {
            lock_guard<mutex> lk(tx_mu);
            txs[tx].status     = TxStatus::Committed;
            txs[tx].commit_ts  = ++commit_clock;
            txs[tx].shrinking  = true;
        }
        release_all(tx);
    }

    void abort(TxId tx) {
        {
            lock_guard<mutex> lk(tx_mu);
            txs[tx].status    = TxStatus::Aborted;
            txs[tx].shrinking = true;
        }
        release_all(tx);
    }

    size_t vacuum() {
        Clock horizon;
        {
            lock_guard<mutex> lk(tx_mu);
            horizon = commit_clock.load();
            for (auto& [_, t] : txs) {
                if (t.status == TxStatus::Active && t.snapshot < horizon)
                    horizon = t.snapshot;
            }
        }
        size_t pruned = 0;
        lock_guard<mutex> lk(heap_mu);
        for (auto& [_, chain] : heap) {
            for (auto it = chain.begin(); it != chain.end();) {
                bool dead = it->xmax != 0
                            && committed_before(it->xmax, horizon);
                if (dead) { it = chain.erase(it); ++pruned; }
                else      { ++it; }
            }
        }
        return pruned;
    }

    size_t chain_length(const Key& k) {
        lock_guard<mutex> lk(heap_mu);
        auto it = heap.find(k);
        return it == heap.end() ? 0 : it->second.size();
    }

private:
    bool committed_before(TxId tx, Clock ts) {
        lock_guard<mutex> lk(tx_mu);
        auto it = txs.find(tx);
        if (it == txs.end() || it->second.status != TxStatus::Committed) return false;
        return it->second.commit_ts <= ts;
    }

    bool is_committed(TxId tx) {
        lock_guard<mutex> lk(tx_mu);
        auto it = txs.find(tx);
        return it != txs.end() && it->second.status == TxStatus::Committed;
    }

    Clock commit_ts_of(TxId tx) {
        lock_guard<mutex> lk(tx_mu);
        auto it = txs.find(tx);
        return it == txs.end() ? 0 : it->second.commit_ts;
    }

    bool visible(const Version& v, TxId reader) {
        Clock snap;
        {
            lock_guard<mutex> lk(tx_mu);
            snap = txs.at(reader).snapshot;
        }
        bool xmin_visible = (v.xmin == reader) || committed_before(v.xmin, snap);
        if (!xmin_visible) return false;
        if (v.xmax == 0) return true;
        bool xmax_visible = (v.xmax == reader) || committed_before(v.xmax, snap);
        return !xmax_visible;
    }

    void check_writable(TxId tx, const list<Version>& chain) {
        Clock snap;
        {
            lock_guard<mutex> lk(tx_mu);
            snap = txs.at(tx).snapshot;
        }
        for (const Version& v : chain) {
            if (v.xmin == tx) continue;
            if (is_committed(v.xmin) && commit_ts_of(v.xmin) > snap)
                throw TxAborted("could not serialize access: row touched by tx " + to_string(v.xmin));
            if (v.xmax != 0 && v.xmax != tx
                && is_committed(v.xmax) && commit_ts_of(v.xmax) > snap)
                throw TxAborted("could not serialize access: row touched by tx " + to_string(v.xmax));
        }
    }

    void acquire(TxId tx, const Key& k, LockMode mode) {
        unique_lock<mutex> lk(lm_mu);

        while (true) {
            {
                lock_guard<mutex> tlk(tx_mu);
                if (txs[tx].status == TxStatus::Aborted)
                    throw TxAborted("aborted by deadlock detector");
                if (txs[tx].shrinking)
                    throw TxAborted("2PL violation: acquire in shrinking phase");
            }
            auto& holders = locks[k];

            bool i_hold_shared    = false;
            bool i_hold_exclusive = false;
            bool conflict         = false;
            for (LockHolder& h : holders) {
                if (h.tx == tx) {
                    if (h.mode == LockMode::Exclusive) i_hold_exclusive = true;
                    else                               i_hold_shared    = true;
                    continue;
                }
                if (mode == LockMode::Exclusive || h.mode == LockMode::Exclusive)
                    conflict = true;
            }

            if (i_hold_exclusive)                              return;
            if (i_hold_shared && mode == LockMode::Shared)     return;

            if (i_hold_shared && mode == LockMode::Exclusive && holders.size() == 1) {
                holders.front().mode = LockMode::Exclusive;
                return;
            }

            if (!conflict && !i_hold_shared) {
                holders.push_back({tx, mode});
                return;
            }

            for (LockHolder& h : holders)
                if (h.tx != tx) waits_for[tx].insert(h.tx);

            if (TxId victim = find_cycle_victim(tx); victim != 0) {
                waits_for.erase(tx);
                if (victim == tx) throw TxAborted("deadlock: victim " + to_string(tx));
                {
                    lock_guard<mutex> tlk(tx_mu);
                    txs[victim].status    = TxStatus::Aborted;
                    txs[victim].shrinking = true;
                }
                purge_locks(victim);
                lm_cv.notify_all();
                continue;
            }

            lm_cv.wait(lk);
            waits_for.erase(tx);
        }
    }

    void release_all(TxId tx) {
        {
            unique_lock<mutex> lk(lm_mu);
            purge_locks(tx);
            waits_for.erase(tx);
            for (auto& [_, deps] : waits_for) deps.erase(tx);
        }
        lm_cv.notify_all();
    }

    void purge_locks(TxId tx) {
        for (auto it = locks.begin(); it != locks.end();) {
            auto& v = it->second;
            v.erase(remove_if(v.begin(), v.end(),
                              [&](const LockHolder& h){ return h.tx == tx; }),
                    v.end());
            if (v.empty()) it = locks.erase(it);
            else           ++it;
        }
    }

    TxId find_cycle_victim(TxId start) {
        unordered_set<TxId> on_stack, done;
        vector<TxId> path;

        function<bool(TxId)> dfs = [&](TxId u) -> bool {
            on_stack.insert(u);
            path.push_back(u);
            for (TxId v : waits_for[u]) {
                if (on_stack.count(v)) { path.push_back(v); return true; }
                if (!done.count(v) && dfs(v)) return true;
            }
            on_stack.erase(u);
            path.pop_back();
            done.insert(u);
            return false;
        };

        if (!dfs(start)) return 0;
        TxId victim = 0;
        for (TxId t : path) if (t > victim) victim = t;
        return victim;
    }

    atomic<TxId>                                next_xid{1};
    atomic<Clock>                               commit_clock{0};

    mutex                                       tx_mu;
    unordered_map<TxId, Transaction>            txs;

    mutex                                       heap_mu;
    unordered_map<Key, list<Version>>           heap;

    mutex                                       lm_mu;
    condition_variable                          lm_cv;
    unordered_map<Key, vector<LockHolder>>      locks;
    unordered_map<TxId, unordered_set<TxId>>    waits_for;
};

static mutex g_io;
static void say(const string& s) {
    lock_guard<mutex> lk(g_io);
    cout << s << "\n";
}

static void scenario_snapshot(TxManager& tm) {
    say("=== 1. snapshot isolation: reader sees pre-write value ===");
    TxId seed = tm.begin();
    tm.write(seed, "balance", "1000");
    tm.commit(seed);

    TxId reader = tm.begin();
    TxId writer = tm.begin();
    tm.write(writer, "balance", "2000");
    tm.commit(writer);

    auto v = tm.read(reader, "balance");
    say("  reader (tx " + to_string(reader) + ") sees: " + v.value_or("<none>"));
    tm.commit(reader);
}

static void scenario_shared(TxManager& tm) {
    say("=== 2. two readers hold shared locks at the same time ===");
    TxId a = tm.begin();
    TxId b = tm.begin();
    auto va = tm.read(a, "balance");
    auto vb = tm.read(b, "balance");
    say("  tx " + to_string(a) + " read: " + va.value_or("<none>"));
    say("  tx " + to_string(b) + " read: " + vb.value_or("<none>"));
    tm.commit(a);
    tm.commit(b);
}

static void scenario_block(TxManager& tm) {
    say("=== 3. exclusive lock blocks a reader, but reader stays at SI snapshot ===");
    TxId writer = tm.begin();
    tm.write(writer, "balance", "3000");

    thread reader_thread([&] {
        TxId r = tm.begin();
        say("  reader (tx " + to_string(r) + ") waiting for shared lock...");
        auto v = tm.read(r, "balance");
        say("  reader (tx " + to_string(r) + ") got: " + v.value_or("<none>"));
        tm.commit(r);
    });

    this_thread::sleep_for(chrono::milliseconds(150));
    tm.commit(writer);
    reader_thread.join();
}

static void scenario_upgrade(TxManager& tm) {
    say("=== 4. lock upgrade S -> X by sole holder ===");
    TxId t = tm.begin();
    auto v = tm.read(t, "balance");
    say("  tx " + to_string(t) + " read under S lock: " + v.value_or("<none>"));
    tm.write(t, "balance", "4000");
    say("  tx " + to_string(t) + " upgraded to X lock and wrote 4000");
    tm.commit(t);
}

static void scenario_deadlock(TxManager& tm) {
    say("=== 5. deadlock detection (younger tx aborts) ===");
    TxId t1 = tm.begin();
    TxId t2 = tm.begin();
    tm.write(t1, "A", "1");
    tm.write(t2, "B", "1");

    atomic<int> aborts{0};
    auto run = [&](TxId tx, const Key& other) {
        try {
            tm.write(tx, other, "2");
            tm.commit(tx);
            say("  tx " + to_string(tx) + " committed");
        } catch (const TxAborted& e) {
            tm.abort(tx);
            aborts++;
            say("  tx " + to_string(tx) + " aborted: " + e.what());
        }
    };
    thread th1(run, t1, "B");
    thread th2(run, t2, "A");
    th1.join();
    th2.join();
    if (aborts.load() == 0) say("  (deadlock detector missed the cycle)");
}

static void scenario_lost_update(TxManager& tm) {
    say("=== 6. SI rejects a lost update (first-updater-wins) ===");
    TxId seed = tm.begin();
    tm.write(seed, "counter", "0");
    tm.commit(seed);

    TxId a = tm.begin();
    TxId b = tm.begin();

    thread th([&] {
        try {
            tm.read(a, "counter");
            tm.write(a, "counter", "1");
            tm.commit(a);
            say("  tx " + to_string(a) + " committed counter=1");
        } catch (const TxAborted& e) {
            tm.abort(a);
            say("  tx " + to_string(a) + " aborted: " + e.what());
        }
    });
    this_thread::sleep_for(chrono::milliseconds(60));
    try {
        tm.read(b, "counter");
        tm.write(b, "counter", "2");
        tm.commit(b);
        say("  tx " + to_string(b) + " committed counter=2");
    } catch (const TxAborted& e) {
        tm.abort(b);
        say("  tx " + to_string(b) + " aborted: " + e.what());
    }
    th.join();
}

static void scenario_vacuum(TxManager& tm) {
    say("=== 7. vacuum prunes dead versions ===");
    for (int i = 0; i < 5; i++) {
        TxId t = tm.begin();
        tm.write(t, "vkey", "v" + to_string(i));
        tm.commit(t);
    }
    say("  vkey chain length before vacuum: " + to_string(tm.chain_length("vkey")));
    size_t pruned = tm.vacuum();
    say("  vacuum pruned " + to_string(pruned) + " dead versions (across all keys)");
    say("  vkey chain length after vacuum:  " + to_string(tm.chain_length("vkey")));
}

int main() {
    TxManager tm;
    scenario_snapshot(tm);
    scenario_shared(tm);
    scenario_block(tm);
    scenario_upgrade(tm);
    scenario_deadlock(tm);
    scenario_lost_update(tm);
    scenario_vacuum(tm);
    return 0;
}
