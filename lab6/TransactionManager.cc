#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <cassert>
#include <functional>

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;
};

static std::atomic<TxID> g_next_xid{1};
static std::mutex g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction() {
    std::lock_guard lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    TxID snap = xid;
    g_transactions[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

struct RowVersion {
    std::string value;
    TxID xmin;
    TxID xmax;
};

static std::mutex g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid)
                 || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    if (v.xmax == 0) return true;

    bool xmax_invisible = (v.xmax == reader_xid)
                        || (is_committed(v.xmax) && v.xmax < snapshot_xid);

    return !xmax_invisible;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }

    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;

    for (auto& v : it->second) {
        if (is_visible(v, snap, xid))
            return v.value;
    }

    return std::nullopt;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }

    auto it = g_heap.find(key);

    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }

    g_heap[key].push_front({new_value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);

    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }

    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;

    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID xid;
    LockMode mode;
    bool granted = false;
};

struct LockQueue {
    std::list<LockRequest> requests;
    std::mutex mu;
    std::condition_variable cv;
};

static std::mutex g_lm_mutex;
static std::unordered_map<RowKey, LockQueue> g_lock_table;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start,
               const std::unordered_map<TxID,
               std::unordered_set<TxID>>& graph) {

    std::unordered_set<TxID> visited, stack;

    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);

        auto it = graph.find(node);

        if (it != graph.end()) {
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb))
                    return true;

                if (stack.count(nb))
                    return true;
            }
        }

        stack.erase(node);
        return false;
    };

    return dfs(start);
}

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error(
            "Deadlock detected, aborting tx " +
            std::to_string(xid)) {}
};

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    {
        std::lock_guard lk(g_tx_mutex);

        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error(
                "2PL violation: lock acquisition in shrinking phase");
    }

    LockQueue& lq = g_lock_table[key];
    std::unique_lock ul(lq.mu);

    lq.requests.push_back({xid, mode, false});
    auto& my_req = lq.requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;

        for (auto& r : lq.requests) {
            if (&r == &my_req) break;
            if (!r.granted) continue;

            if (mode == LockMode::EXCLUSIVE ||
                r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) {
                    conflict = true;
                    blocking.insert(r.xid);
                }
            }
        }

        if (!conflict) {
            my_req.granted = true;

            {
                std::lock_guard lk(g_lm_mutex);
                g_waits_for.erase(xid);
            }

            return;
        }

        {
            std::lock_guard lk(g_lm_mutex);

            g_waits_for[xid] = blocking;

            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);

                lq.requests.remove_if([&](const LockRequest& r) {
                    return r.xid == xid && !r.granted;
                });

                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);
    }
}

void release_locks(TxID xid) {
    {
        std::lock_guard lk(g_tx_mutex);

        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    for (auto& [key, lq] : g_lock_table) {
        std::unique_lock ul(lq.mu);

        lq.requests.remove_if([&](const LockRequest& r) {
            return r.xid == xid;
        });

        lq.cv.notify_all();
    }

    {
        std::lock_guard lk(g_lm_mutex);
        g_waits_for.erase(xid);
    }
}

class TransactionManager {
public:
    TxID begin() {
        return begin_transaction();
    }

    std::optional<std::string> read(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::SHARED);
        return mvcc_read_key(key, xid);
    }

    void insert(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_insert(key, value, xid);
    }

    void update(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_update(key, value, xid);
    }

    void remove(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
    }

    void commit(TxID xid) {
        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }

        release_locks(xid);

        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        {
            std::lock_guard lk(g_heap_mutex);

            for (auto& [key, chain] : g_heap) {
                for (auto& v : chain) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid) v.xmax = 0;
                }
            }
        }

        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }

        release_locks(xid);

        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

int main() {
    TransactionManager tm;

    TxID t1 = tm.begin();
    tm.insert(t1, "balance", "1000");
    tm.commit(t1);

    TxID t2 = tm.begin();
    auto value = tm.read(t2, "balance");

    if (value)
        std::cout << "balance = " << *value << "\n";

    tm.commit(t2);

    return 0;
}
