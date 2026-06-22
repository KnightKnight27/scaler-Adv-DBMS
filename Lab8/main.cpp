#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <sstream>
#include <cassert>
#include <functional>

// ---------------------------------------------------------------------
// Multi-Version Concurrency Control (MVCC) with Strict 2PL & Deadlocks
// Student: Ayush Kumar Patra (24bcs10474)
// Course: Advanced Database Management Systems (ADBMS)
// ---------------------------------------------------------------------

using TransactionHandle = uint64_t;
using DataKey = std::string;

enum class TxState { ACTIVE, COMMITTED, ABORTED };

struct TxnInfo {
    TransactionHandle id;
    TransactionHandle snapshot_id;
    TxState state = TxState::ACTIVE;
    bool lock_shrinking = false;
};

static std::atomic<TransactionHandle> global_next_tx{1};
static std::mutex tx_meta_mutex;
static std::unordered_map<TransactionHandle, TxnInfo> transactions_map;

TransactionHandle startNewTransaction() {
    std::lock_guard lk(tx_meta_mutex);
    TransactionHandle xid = global_next_tx.fetch_add(1);
    TransactionHandle snap = xid;
    transactions_map[xid] = TxnInfo{xid, snap, TxState::ACTIVE, false};
    return xid;
}

bool hasCommitted(TransactionHandle xid) {
    std::lock_guard lk(tx_meta_mutex);
    auto it = transactions_map.find(xid);
    return it != transactions_map.end() && it->second.state == TxState::COMMITTED;
}

bool hasAborted(TransactionHandle xid) {
    std::lock_guard lk(tx_meta_mutex);
    auto it = transactions_map.find(xid);
    return it != transactions_map.end() && it->second.state == TxState::ABORTED;
}

struct RecordVersion {
    std::string data;
    TransactionHandle xmin;
    TransactionHandle xmax = 0;
};

static std::mutex heap_storage_mutex;
static std::unordered_map<DataKey, std::list<RecordVersion>> heap_table;

bool checkVisibility(const RecordVersion& v, TransactionHandle snapshot_id, TransactionHandle reader_id) {
    bool xmin_valid = (v.xmin == reader_id) || (hasCommitted(v.xmin) && v.xmin < snapshot_id);
    if (!xmin_valid) return false;
    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_id) || (hasCommitted(v.xmax) && v.xmax < snapshot_id);
    return !xmax_invisible;
}

std::optional<std::string> mvccReadRecord(const DataKey& key, TransactionHandle xid) {
    std::lock_guard lk(heap_storage_mutex);
    TransactionHandle snap;
    {
        std::lock_guard tlk(tx_meta_mutex);
        snap = transactions_map.at(xid).snapshot_id;
    }
    auto it = heap_table.find(key);
    if (it == heap_table.end()) return std::nullopt;
    for (auto& v : it->second) {
        if (checkVisibility(v, snap, xid)) return v.data;
    }
    return std::nullopt;
}

void mvccInsertRecord(const DataKey& key, const std::string& val, TransactionHandle xid) {
    std::lock_guard lk(heap_storage_mutex);
    heap_table[key].push_front({val, xid, 0});
}

void mvccUpdateRecord(const DataKey& key, const std::string& new_val, TransactionHandle xid) {
    std::lock_guard lk(heap_storage_mutex);
    TransactionHandle snap;
    {
        std::lock_guard tlk(tx_meta_mutex);
        snap = transactions_map.at(xid).snapshot_id;
    }
    auto it = heap_table.find(key);
    if (it != heap_table.end()) {
        for (auto& v : it->second) {
            if (checkVisibility(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }
    heap_table[key].push_front({new_val, xid, 0});
}

void mvccDeleteRecord(const DataKey& key, TransactionHandle xid) {
    std::lock_guard lk(heap_storage_mutex);
    TransactionHandle snap;
    {
        std::lock_guard tlk(tx_meta_mutex);
        snap = transactions_map.at(xid).snapshot_id;
    }
    auto it = heap_table.find(key);
    if (it == heap_table.end()) return;
    for (auto& v : it->second) {
        if (checkVisibility(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

enum class LockAccess { SHARED, EXCLUSIVE };

struct LockTicket {
    TransactionHandle xid;
    LockAccess access;
    bool granted = false;
};

struct LockWaitList {
    std::list<LockTicket> tickets;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
};

static std::mutex lock_mgr_mutex;
static std::unordered_map<DataKey, LockWaitList> lock_directory;
static std::unordered_map<TransactionHandle, std::unordered_set<TransactionHandle>> waits_for_relations;

bool detectCycleDFS(TransactionHandle start, const std::unordered_map<TransactionHandle, std::unordered_set<TransactionHandle>>& graph) {
    std::unordered_set<TransactionHandle> visited, recursion_stack;
    std::function<bool(TransactionHandle)> dfs = [&](TransactionHandle node) -> bool {
        visited.insert(node);
        recursion_stack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TransactionHandle neighbor : it->second) {
                if (!visited.count(neighbor) && dfs(neighbor)) return true;
                if (recursion_stack.count(neighbor)) return true;
            }
        }
        recursion_stack.erase(node);
        return false;
    };
    return dfs(start);
}

class CircularWaitException : public std::runtime_error {
public:
    explicit CircularWaitException(TransactionHandle xid)
        : std::runtime_error("Circular dependency (deadlock) detected. Aborting transaction #" + std::to_string(xid)) {}
};

void requestLock(const DataKey& key, TransactionHandle xid, LockAccess access) {
    {
        std::lock_guard lk(tx_meta_mutex);
        if (transactions_map.at(xid).lock_shrinking) {
            throw std::runtime_error("2PL violation: lock requests prohibited during shrinking phase");
        }
    }

    LockWaitList& wl = lock_directory[key];
    std::unique_lock ul(wl.queue_mutex);

    for (auto& t : wl.tickets) {
        if (t.xid == xid && t.granted) {
            if (access == LockAccess::SHARED) return;
            if (t.access == LockAccess::EXCLUSIVE) return;
        }
    }

    wl.tickets.push_back({xid, access, false});
    auto& my_ticket = wl.tickets.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TransactionHandle> blocking_txs;
        for (auto& t : wl.tickets) {
            if (&t == &my_ticket) break;
            if (!t.granted) continue;
            if (access == LockAccess::EXCLUSIVE || t.access == LockAccess::EXCLUSIVE) {
                if (t.xid != xid) {
                    conflict = true;
                    blocking_txs.insert(t.xid);
                }
            }
        }

        if (!conflict) {
            my_ticket.granted = true;
            {
                std::lock_guard lk(lock_mgr_mutex);
                waits_for_relations.erase(xid);
            }
            return;
        }

        {
            std::lock_guard lk(lock_mgr_mutex);
            waits_for_relations[xid] = blocking_txs;
            if (detectCycleDFS(xid, waits_for_relations)) {
                waits_for_relations.erase(xid);
                wl.tickets.remove_if([&](const LockTicket& t){ return t.xid == xid && !t.granted; });
                throw CircularWaitException(xid);
            }
        }

        wl.queue_cv.wait(ul);
    }
}

void releaseTxnLocks(TransactionHandle xid) {
    {
        std::lock_guard lk(tx_meta_mutex);
        if (transactions_map.count(xid)) {
            transactions_map.at(xid).lock_shrinking = true;
        }
    }

    for (auto& [key, wl] : lock_directory) {
        std::unique_lock ul(wl.queue_mutex);
        wl.tickets.remove_if([&](const LockTicket& t){ return t.xid == xid; });
        wl.queue_cv.notify_all();
    }

    {
        std::lock_guard lk(lock_mgr_mutex);
        waits_for_relations.erase(xid);
    }
}

class ConcurrencyManager {
public:
    TransactionHandle begin() { return startNewTransaction(); }

    std::optional<std::string> read(TransactionHandle xid, const DataKey& key) {
        requestLock(key, xid, LockAccess::SHARED);
        return mvccReadRecord(key, xid);
    }

    void insert(TransactionHandle xid, const DataKey& key, const std::string& value) {
        requestLock(key, xid, LockAccess::EXCLUSIVE);
        mvccInsertRecord(key, value, xid);
    }

    void update(TransactionHandle xid, const DataKey& key, const std::string& value) {
        requestLock(key, xid, LockAccess::EXCLUSIVE);
        mvccUpdateRecord(key, value, xid);
    }

    void remove(TransactionHandle xid, const DataKey& key) {
        requestLock(key, xid, LockAccess::EXCLUSIVE);
        mvccDeleteRecord(key, xid);
    }

    void commit(TransactionHandle xid) {
        {
            std::lock_guard lk(tx_meta_mutex);
            transactions_map.at(xid).state = TxState::COMMITTED;
        }
        releaseTxnLocks(xid);
        std::cout << "[CONCURRENCY] Txn #" << xid << " COMMITTED\n";
    }

    void abort(TransactionHandle xid) {
        {
            std::lock_guard lk(heap_storage_mutex);
            for (auto& [key, list] : heap_table) {
                for (auto& v : list) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid) v.xmax = 0;
                }
            }
        }
        {
            std::lock_guard lk(tx_meta_mutex);
            transactions_map.at(xid).state = TxState::ABORTED;
        }
        releaseTxnLocks(xid);
        std::cout << "[CONCURRENCY] Txn #" << xid << " ABORTED\n";
    }
};

void print_val(const std::optional<std::string>& v, TransactionHandle xid, const DataKey& key) {
    std::cout << "  [Txn #" << xid << "] READ " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main() {
    ConcurrencyManager mgr;

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TransactionHandle t1 = mgr.begin();
        mgr.insert(t1, "resource_val", "1000");
        mgr.commit(t1);

        TransactionHandle t2 = mgr.begin();
        TransactionHandle t3 = mgr.begin();

        mgr.update(t3, "resource_val", "2000");
        mgr.commit(t3);

        auto v = mgr.read(t2, "resource_val");
        print_val(v, t2, "resource_val");
        mgr.commit(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TransactionHandle t4 = mgr.begin();
        TransactionHandle t5 = mgr.begin();
        print_val(mgr.read(t4, "resource_val"), t4, "resource_val");
        print_val(mgr.read(t5, "resource_val"), t5, "resource_val");
        mgr.commit(t4);
        mgr.commit(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TransactionHandle t6 = mgr.begin();
        mgr.update(t6, "resource_val", "3000");

        std::thread reader([&]() {
            TransactionHandle t7 = mgr.begin();
            std::cout << "  [Txn #" << t7 << "] waiting for shared lock on resource_val...\n";
            auto v = mgr.read(t7, "resource_val");
            print_val(v, t7, "resource_val");
            mgr.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.commit(t6);
        reader.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TransactionHandle ta = mgr.begin();
        TransactionHandle tb = mgr.begin();

        mgr.insert(ta, "res_X", "val_x");
        mgr.insert(tb, "res_Y", "val_y");
        mgr.commit(ta);
        mgr.commit(tb);

        TransactionHandle t8 = mgr.begin();
        TransactionHandle t9 = mgr.begin();

        requestLock("res_X", t8, LockAccess::EXCLUSIVE);
        requestLock("res_Y", t9, LockAccess::EXCLUSIVE);

        std::thread th1([&]() {
            try {
                requestLock("res_Y", t8, LockAccess::EXCLUSIVE);
                mgr.commit(t8);
            } catch (CircularWaitException& e) {
                std::cout << "  " << e.what() << "\n";
                mgr.abort(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            requestLock("res_X", t9, LockAccess::EXCLUSIVE);
            mgr.commit(t9);
        } catch (CircularWaitException& e) {
            std::cout << "  " << e.what() << "\n";
            mgr.abort(t9);
        }

        th1.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
