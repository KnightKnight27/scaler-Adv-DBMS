// Lab 8: Transaction Manager — MVCC + Strict 2PL + Deadlock Detection
// Build: g++ -std=c++17 -pthread -o txmgr txmgr.cpp && ./txmgr

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <functional>
#include <chrono>

using TxnId   = uint64_t;
using RecordKey = std::string;

enum class TxnState { RUNNING, DONE, KILLED };

struct Txn {
    TxnId   tid;
    TxnId   snapshot_at;
    TxnState state = TxnState::RUNNING;
    bool    shrinking = false;
};

static std::atomic<TxnId>                    g_next_id{1};
static std::mutex                            g_txn_lock;
static std::unordered_map<TxnId, Txn>        g_txns;

TxnId start_txn() {
    std::lock_guard g(g_txn_lock);
    TxnId id = g_next_id.fetch_add(1);
    g_txns[id] = Txn{id, id, TxnState::RUNNING, false};
    return id;
}

bool check_committed(TxnId tid) {
    std::lock_guard g(g_txn_lock);
    auto it = g_txns.find(tid);
    return it != g_txns.end() && it->second.state == TxnState::DONE;
}

// ---------- MVCC version store ----------

struct Version {
    std::string data;
    TxnId       born_at;
    TxnId       died_at;
};

static std::mutex                                         g_store_lock;
static std::unordered_map<RecordKey, std::list<Version>>  g_store;

bool version_visible(const Version& ver, TxnId snap_tid, TxnId reader_tid) {
    bool creator_ok = (ver.born_at == reader_tid)
                   || (check_committed(ver.born_at) && ver.born_at < snap_tid);
    if (!creator_ok) return false;

    if (ver.died_at == 0) return true;
    bool death_hidden = (ver.died_at == reader_tid)
                     || (check_committed(ver.died_at) && ver.died_at < snap_tid);
    return !death_hidden;
}

TxnId txn_snapshot(TxnId tid) {
    std::lock_guard g(g_txn_lock);
    return g_txns.at(tid).snapshot_at;
}

std::optional<std::string> mvcc_get(const RecordKey& key, TxnId tid) {
    std::lock_guard g(g_store_lock);
    TxnId snap = txn_snapshot(tid);
    auto it = g_store.find(key);
    if (it == g_store.end()) return std::nullopt;
    for (auto& v : it->second)
        if (version_visible(v, snap, tid)) return v.data;
    return std::nullopt;
}

void mvcc_add(const RecordKey& key, const std::string& val, TxnId tid) {
    std::lock_guard g(g_store_lock);
    g_store[key].push_front({val, tid, 0});
}

void mvcc_set(const RecordKey& key, const std::string& new_val, TxnId tid) {
    std::lock_guard g(g_store_lock);
    TxnId snap = txn_snapshot(tid);
    auto it = g_store.find(key);
    if (it != g_store.end()) {
        for (auto& v : it->second) {
            if (version_visible(v, snap, tid) && v.died_at == 0) {
                v.died_at = tid;
                break;
            }
        }
    }
    g_store[key].push_front({new_val, tid, 0});
}

void mvcc_remove(const RecordKey& key, TxnId tid) {
    std::lock_guard g(g_store_lock);
    TxnId snap = txn_snapshot(tid);
    auto it = g_store.find(key);
    if (it == g_store.end()) return;
    for (auto& v : it->second) {
        if (version_visible(v, snap, tid) && v.died_at == 0) {
            v.died_at = tid;
            return;
        }
    }
}

// ---------- Lock Manager (Strict 2PL) ----------

enum class LockKind { READ, WRITE };

struct LockEntry {
    TxnId   tid;
    LockKind kind;
    bool    acquired = false;
};

struct LockQueue {
    std::list<LockEntry>         entries;
    std::mutex                   mtx;
    std::condition_variable      cv;
};

static std::mutex                                        g_lk_mutex;
static std::unordered_map<RecordKey, LockQueue>          g_lock_map;
static std::unordered_map<TxnId, std::unordered_set<TxnId>> g_wait_graph;

bool detect_cycle(TxnId start, const std::unordered_map<TxnId, std::unordered_set<TxnId>>& graph) {
    std::unordered_set<TxnId> seen, path;
    std::function<bool(TxnId)> traverse = [&](TxnId node) -> bool {
        seen.insert(node);
        path.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxnId neighbor : it->second) {
                if (!seen.count(neighbor) && traverse(neighbor)) return true;
                if (path.count(neighbor)) return true;
            }
        }
        path.erase(node);
        return false;
    };
    return traverse(start);
}

class DeadlockError : public std::runtime_error {
public:
    explicit DeadlockError(TxnId tid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(tid)) {}
};

void request_lock(const RecordKey& key, TxnId tid, LockKind kind) {
    {
        std::lock_guard g(g_txn_lock);
        if (g_txns.at(tid).shrinking)
            throw std::runtime_error("2PL violation: lock after shrink phase");
    }

    LockQueue& lq = g_lock_map[key];
    std::unique_lock ul(lq.mtx);

    for (auto& e : lq.entries) {
        if (e.tid == tid && e.acquired) {
            if (kind == LockKind::READ) return;
            if (e.kind == LockKind::WRITE) return;
        }
    }

    lq.entries.push_back({tid, kind, false});
    auto& mine = lq.entries.back();

    while (true) {
        bool blocked = false;
        std::unordered_set<TxnId> holders;
        for (auto& e : lq.entries) {
            if (&e == &mine) break;
            if (!e.acquired) continue;
            if (kind == LockKind::WRITE || e.kind == LockKind::WRITE) {
                if (e.tid != tid) { blocked = true; holders.insert(e.tid); }
            }
        }

        if (!blocked) {
            mine.acquired = true;
            std::lock_guard g(g_lk_mutex);
            g_wait_graph.erase(tid);
            return;
        }

        {
            std::lock_guard g(g_lk_mutex);
            g_wait_graph[tid] = holders;
            if (detect_cycle(tid, g_wait_graph)) {
                g_wait_graph.erase(tid);
                lq.entries.remove_if([&](const LockEntry& e){ return e.tid == tid && !e.acquired; });
                throw DeadlockError(tid);
            }
        }

        lq.cv.wait(ul);
    }
}

void drop_locks(TxnId tid) {
    {
        std::lock_guard g(g_txn_lock);
        if (g_txns.count(tid))
            g_txns.at(tid).shrinking = true;
    }

    for (auto& [key, lq] : g_lock_map) {
        std::unique_lock ul(lq.mtx);
        lq.entries.remove_if([&](const LockEntry& e){ return e.tid == tid; });
        lq.cv.notify_all();
    }

    std::lock_guard g(g_lk_mutex);
    g_wait_graph.erase(tid);
}

// ---------- Transaction Manager ----------

class TxnManager {
public:
    TxnId begin() { return start_txn(); }

    std::optional<std::string> read(TxnId tid, const RecordKey& key) {
        request_lock(key, tid, LockKind::READ);
        return mvcc_get(key, tid);
    }

    void add(TxnId tid, const RecordKey& key, const std::string& val) {
        request_lock(key, tid, LockKind::WRITE);
        mvcc_add(key, val, tid);
    }

    void write(TxnId tid, const RecordKey& key, const std::string& val) {
        request_lock(key, tid, LockKind::WRITE);
        mvcc_set(key, val, tid);
    }

    void erase(TxnId tid, const RecordKey& key) {
        request_lock(key, tid, LockKind::WRITE);
        mvcc_remove(key, tid);
    }

    void done(TxnId tid) {
        {
            std::lock_guard g(g_txn_lock);
            g_txns.at(tid).state = TxnState::DONE;
        }
        drop_locks(tid);
        std::cout << "[TX " << tid << "] DONE\n";
    }

    void kill(TxnId tid) {
        {
            std::lock_guard g(g_store_lock);
            for (auto& [key, chain] : g_store) {
                for (auto& v : chain) {
                    if (v.born_at == tid) v.died_at = tid;
                    if (v.died_at == tid) v.died_at = 0;
                }
            }
        }
        {
            std::lock_guard g(g_txn_lock);
            g_txns.at(tid).state = TxnState::KILLED;
        }
        drop_locks(tid);
        std::cout << "[TX " << tid << "] KILLED\n";
    }
};

// ---------- Helpers ----------

void show_val(const std::optional<std::string>& val, TxnId tid, const RecordKey& key) {
    std::cout << "  [TX " << tid << "] GET " << key << " = "
              << (val ? *val : "<hidden>") << "\n";
}

// ---------- Main ----------

int main() {
    TxnManager mgr;

    std::cout << "=== Test 1: MVCC Snapshot Isolation ===\n";
    {
        TxnId t1 = mgr.begin();
        mgr.add(t1, "balance", "1000");
        mgr.done(t1);

        TxnId t2 = mgr.begin();
        TxnId t3 = mgr.begin();

        mgr.write(t3, "balance", "2000");
        mgr.done(t3);

        show_val(mgr.read(t2, "balance"), t2, "balance");
        mgr.done(t2);
    }

    std::cout << "\n=== Test 2: Multiple Shared Locks ===\n";
    {
        TxnId t4 = mgr.begin();
        TxnId t5 = mgr.begin();
        show_val(mgr.read(t4, "balance"), t4, "balance");
        show_val(mgr.read(t5, "balance"), t5, "balance");
        mgr.done(t4);
        mgr.done(t5);
    }

    std::cout << "\n=== Test 3: Exclusive Lock Blocks ===\n";
    {
        TxnId t6 = mgr.begin();
        mgr.write(t6, "balance", "3000");

        std::thread worker([&]() {
            TxnId t7 = mgr.begin();
            std::cout << "  [TX " << t7 << "] waiting on balance...\n";
            show_val(mgr.read(t7, "balance"), t7, "balance");
            mgr.done(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.done(t6);
        worker.join();
    }

    std::cout << "\n=== Test 4: Deadlock Resolution ===\n";
    {
        TxnId ta = mgr.begin();
        TxnId tb = mgr.begin();
        mgr.add(ta, "A", "x_a");
        mgr.add(tb, "B", "x_b");
        mgr.done(ta);
        mgr.done(tb);

        TxnId t8 = mgr.begin();
        TxnId t9 = mgr.begin();
        request_lock("A", t8, LockKind::WRITE);
        request_lock("B", t9, LockKind::WRITE);

        std::thread th1([&]() {
            try {
                request_lock("B", t8, LockKind::WRITE);
                mgr.done(t8);
            } catch (DeadlockError& e) {
                std::cout << "  " << e.what() << "\n";
                mgr.kill(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            request_lock("A", t9, LockKind::WRITE);
            mgr.done(t9);
        } catch (DeadlockError& e) {
            std::cout << "  " << e.what() << "\n";
            mgr.kill(t9);
        }

        th1.join();
    }

    std::cout << "\nAll scenarios finished.\n";
    return 0;
}
