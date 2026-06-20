#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <functional>
#include <chrono>

using TxID = uint64_t;
using RowKey = std::string;

enum class TxStatus {
    ACTIVE,
    COMMITTED,
    ABORTED
};

struct Transaction {
    TxID id;
    TxID snapshot_xid;
    TxStatus status = TxStatus::ACTIVE;
    bool in_shrinking = false;
};

static std::atomic<TxID> g_next_xid{1};
static std::mutex g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

// =====================================================
// Transaction Table
// =====================================================

TxID begin_transaction() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);

    TxID xid = g_next_xid.fetch_add(1);
    TxID snap = xid;

    g_transactions[xid] = {
        xid,
        snap,
        TxStatus::ACTIVE,
        false
    };

    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);

    auto it = g_transactions.find(xid);
    return it != g_transactions.end() &&
           it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);

    auto it = g_transactions.find(xid);
    return it != g_transactions.end() &&
           it->second.status == TxStatus::ABORTED;
}

// =====================================================
// MVCC Heap
// =====================================================

struct RowVersion {
    std::string value;
    TxID xmin;
    TxID xmax;
};

static std::mutex g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(
    const RowVersion& v,
    TxID snapshot_xid,
    TxID reader_xid
) {
    bool xmin_ok =
        (v.xmin == reader_xid) ||
        (is_committed(v.xmin) && v.xmin < snapshot_xid);

    if (!xmin_ok)
        return false;

    if (v.xmax == 0)
        return true;

    bool xmax_invisible =
        (v.xmax == reader_xid) ||
        (is_committed(v.xmax) && v.xmax < snapshot_xid);

    return !xmax_invisible;
}

std::optional<std::string> mvcc_read_key(
    const RowKey& key,
    TxID xid
) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);

    TxID snapshot;

    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snapshot = g_transactions.at(xid).snapshot_xid;
    }

    auto it = g_heap.find(key);

    if (it == g_heap.end())
        return std::nullopt;

    for (auto& version : it->second) {
        if (is_visible(version, snapshot, xid))
            return version.value;
    }

    return std::nullopt;
}

void mvcc_insert(
    const RowKey& key,
    const std::string& value,
    TxID xid
) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);

    g_heap[key].push_front({
        value,
        xid,
        0
    });
}

void mvcc_update(
    const RowKey& key,
    const std::string& value,
    TxID xid
) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);

    TxID snapshot;

    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snapshot = g_transactions.at(xid).snapshot_xid;
    }

    auto it = g_heap.find(key);

    if (it != g_heap.end()) {
        for (auto& version : it->second) {
            if (is_visible(version, snapshot, xid) &&
                version.xmax == 0) {
                version.xmax = xid;
                break;
            }
        }
    }

    g_heap[key].push_front({
        value,
        xid,
        0
    });
}

void mvcc_delete(
    const RowKey& key,
    TxID xid
) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);

    TxID snapshot;

    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snapshot = g_transactions.at(xid).snapshot_xid;
    }

    auto it = g_heap.find(key);

    if (it == g_heap.end())
        return;

    for (auto& version : it->second) {
        if (is_visible(version, snapshot, xid) &&
            version.xmax == 0) {
            version.xmax = xid;
            return;
        }
    }
}

// =====================================================
// Lock Manager
// =====================================================

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

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

static std::unordered_map<RowKey, LockQueue> g_lock_table;
static std::mutex g_lm_mutex;

static std::unordered_map<
    TxID,
    std::unordered_set<TxID>
> g_waits_for;

bool has_cycle(
    TxID start,
    const std::unordered_map<
        TxID,
        std::unordered_set<TxID>
    >& graph
) {
    std::unordered_set<TxID> visited;
    std::unordered_set<TxID> stack;

    std::function<bool(TxID)> dfs =
        [&](TxID node) -> bool {

        visited.insert(node);
        stack.insert(node);

        auto it = graph.find(node);

        if (it != graph.end()) {
            for (auto neighbor : it->second) {

                if (!visited.count(neighbor)) {
                    if (dfs(neighbor))
                        return true;
                }

                if (stack.count(neighbor))
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

void acquire_lock(
    const RowKey& key,
    TxID xid,
    LockMode mode
) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);

        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error(
                "2PL violation");
    }

    LockQueue& queue = g_lock_table[key];

    std::unique_lock<std::mutex> ul(queue.mu);

    queue.requests.push_back({
        xid,
        mode,
        false
    });

    auto& my_req = queue.requests.back();

    while (true) {

        bool conflict = false;
        std::unordered_set<TxID> blockers;

        for (auto& req : queue.requests) {

            if (&req == &my_req)
                break;

            if (!req.granted)
                continue;

            if (mode == LockMode::EXCLUSIVE ||
                req.mode == LockMode::EXCLUSIVE) {

                if (req.xid != xid) {
                    conflict = true;
                    blockers.insert(req.xid);
                }
            }
        }

        if (!conflict) {

            my_req.granted = true;

            {
                std::lock_guard<std::mutex> lk(g_lm_mutex);
                g_waits_for.erase(xid);
            }

            return;
        }

        {
            std::lock_guard<std::mutex> lk(g_lm_mutex);

            g_waits_for[xid] = blockers;

            if (has_cycle(xid, g_waits_for)) {

                g_waits_for.erase(xid);

                queue.requests.remove_if(
                    [&](const LockRequest& r) {
                        return r.xid == xid &&
                               !r.granted;
                    });

                throw DeadlockException(xid);
            }
        }

        queue.cv.wait(ul);
    }
}

void release_locks(TxID xid) {

    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);

        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    for (auto& [key, queue] : g_lock_table) {

        std::unique_lock<std::mutex> ul(queue.mu);

        queue.requests.remove_if(
            [&](const LockRequest& r) {
                return r.xid == xid;
            });

        queue.cv.notify_all();
    }

    {
        std::lock_guard<std::mutex> lk(g_lm_mutex);
        g_waits_for.erase(xid);
    }
}

// =====================================================
// Transaction Manager
// =====================================================

class TransactionManager {
public:

    TxID begin() {
        return begin_transaction();
    }

    std::optional<std::string> read(
        TxID xid,
        const RowKey& key
    ) {
        acquire_lock(key, xid, LockMode::SHARED);
        return mvcc_read_key(key, xid);
    }

    void insert(
        TxID xid,
        const RowKey& key,
        const std::string& value
    ) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_insert(key, value, xid);
    }

    void update(
        TxID xid,
        const RowKey& key,
        const std::string& value
    ) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_update(key, value, xid);
    }

    void remove(
        TxID xid,
        const RowKey& key
    ) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
    }

    void commit(TxID xid) {

        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status =
                TxStatus::COMMITTED;
        }

        release_locks(xid);

        std::cout
            << "[TX " << xid
            << "] COMMITTED\n";
    }

    void abort(TxID xid) {

        {
            std::lock_guard<std::mutex> lk(g_heap_mutex);

            for (auto& [key, chain] : g_heap) {

                for (auto& v : chain) {

                    if (v.xmin == xid)
                        v.xmax = xid;

                    if (v.xmax == xid)
                        v.xmax = 0;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status =
                TxStatus::ABORTED;
        }

        release_locks(xid);

        std::cout
            << "[TX " << xid
            << "] ABORTED\n";
    }
};

// =====================================================
// Demo
// =====================================================

void print_value(
    const std::optional<std::string>& value,
    TxID xid,
    const RowKey& key
) {
    std::cout
        << "  [TX " << xid
        << "] READ "
        << key
        << " = "
        << (value ? *value : "<not visible>")
        << "\n";
}

int main() {

    TransactionManager tm;

    std::cout
        << "=== Scenario 1: MVCC Snapshot Isolation ===\n";

    TxID t1 = tm.begin();
    tm.insert(t1, "balance", "1000");
    tm.commit(t1);

    TxID t2 = tm.begin();
    TxID t3 = tm.begin();

    tm.update(t3, "balance", "2000");
    tm.commit(t3);

    print_value(
        tm.read(t2, "balance"),
        t2,
        "balance"
    );

    tm.commit(t2);

    std::cout
        << "\n=== Scenario 2: Concurrent Shared Locks ===\n";

    TxID t4 = tm.begin();
    TxID t5 = tm.begin();

    print_value(tm.read(t4, "balance"), t4, "balance");
    print_value(tm.read(t5, "balance"), t5, "balance");

    tm.commit(t4);
    tm.commit(t5);

    std::cout
        << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";

    TxID t6 = tm.begin();

    tm.update(t6, "balance", "3000");

    std::thread reader([&]() {

        TxID t7 = tm.begin();

        std::cout
            << "  [TX " << t7
            << "] waiting for shared lock...\n";

        auto value = tm.read(t7, "balance");

        print_value(value, t7, "balance");

        tm.commit(t7);
    });

    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));

    tm.commit(t6);

    reader.join();

    std::cout
        << "\nAll scenarios completed.\n";

    return 0;
}