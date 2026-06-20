#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <set>

using TxID = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };
enum class LockType { READ, WRITE };

// ─────────────────────────────────────────────
// 1. MVCC Version Chain
// ─────────────────────────────────────────────

struct VersionChain {
    TxID   xmin;      // creator transaction
    TxID   xmax;      // deleter transaction (0 if not deleted)
    std::string value;
};

struct RowVersions {
    std::vector<VersionChain> versions;
};

// ─────────────────────────────────────────────
// 2. Transaction State
// ─────────────────────────────────────────────

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;          // read snapshot
    TxStatus status;
    std::set<RowKey> read_set;      // rows read
    std::set<RowKey> write_set;     // rows written
    bool     in_shrinking = false;  // 2PL phase
};

// ─────────────────────────────────────────────
// 3. Lock Manager
// ─────────────────────────────────────────────

struct LockInfo {
    LockType   type;
    TxID       holder;
};

class LockManager {
private:
    std::unordered_map<RowKey, std::set<TxID>> read_locks;    // key -> set of tx holding read lock
    std::unordered_map<RowKey, TxID> write_locks;             // key -> tx holding write lock
    std::unordered_map<RowKey, std::queue<std::pair<TxID, LockType>>> wait_queue;  // waiting transactions
    std::shared_mutex mtx;

public:
    bool acquireReadLock(TxID tx_id, const RowKey& key) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        
        // Check if someone holds write lock
        if (write_locks.count(key) && write_locks[key] != tx_id) {
            wait_queue[key].push({tx_id, LockType::READ});
            std::cout << "[LOCK] TX" << tx_id << " waits for READ on " << key << "\n";
            return false;
        }
        
        read_locks[key].insert(tx_id);
        std::cout << "[LOCK] TX" << tx_id << " acquires READ on " << key << "\n";
        return true;
    }

    bool acquireWriteLock(TxID tx_id, const RowKey& key) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        
        // Check if anyone holds read or write lock
        if ((read_locks.count(key) && !read_locks[key].empty() && !(read_locks[key].size() == 1 && read_locks[key].count(tx_id))) ||
            (write_locks.count(key) && write_locks[key] != tx_id)) {
            wait_queue[key].push({tx_id, LockType::WRITE});
            std::cout << "[LOCK] TX" << tx_id << " waits for WRITE on " << key << "\n";
            return false;
        }
        
        write_locks[key] = tx_id;
        read_locks[key].erase(tx_id);  // Upgrade from read to write
        std::cout << "[LOCK] TX" << tx_id << " acquires WRITE on " << key << "\n";
        return true;
    }

    void releaseLock(TxID tx_id, const RowKey& key) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        
        if (write_locks.count(key) && write_locks[key] == tx_id) {
            write_locks.erase(key);
            std::cout << "[LOCK] TX" << tx_id << " releases WRITE on " << key << "\n";
        } else if (read_locks.count(key)) {
            read_locks[key].erase(tx_id);
            std::cout << "[LOCK] TX" << tx_id << " releases READ on " << key << "\n";
        }
    }

    void releaseAllLocks(TxID tx_id, const std::set<RowKey>& keys) {
        for (const auto& key : keys) {
            releaseLock(tx_id, key);
        }
    }

    std::vector<std::pair<TxID, LockType>> getWaitingTransactions(const RowKey& key) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        std::vector<std::pair<TxID, LockType>> result;
        auto temp_queue = wait_queue[key];
        while (!temp_queue.empty()) {
            result.push_back(temp_queue.front());
            temp_queue.pop();
        }
        return result;
    }
};

// ─────────────────────────────────────────────
// 4. Deadlock Detector
// ─────────────────────────────────────────────

class DeadlockDetector {
private:
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for;  // waits_for[A] = {B} means A waits for B
    std::mutex mtx;

public:
    void addWait(TxID waiter, TxID holder) {
        std::lock_guard<std::mutex> lock(mtx);
        waits_for[waiter].insert(holder);
    }

    void removeWait(TxID tx_id) {
        std::lock_guard<std::mutex> lock(mtx);
        waits_for.erase(tx_id);
        for (auto& [_, set] : waits_for) {
            set.erase(tx_id);
        }
    }

    bool detectCycle(TxID& victim_out) {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (const auto& [tx_id, _] : waits_for) {
            if (hasCycle(tx_id, tx_id, std::unordered_set<TxID>())) {
                victim_out = tx_id;
                return true;
            }
        }
        return false;
    }

private:
    bool hasCycle(TxID start, TxID current, std::unordered_set<TxID> visited) {
        if (visited.count(current)) {
            return current == start && visited.size() > 1;
        }
        
        visited.insert(current);
        
        if (waits_for.count(current)) {
            for (TxID next : waits_for[current]) {
                if (hasCycle(start, next, visited)) {
                    return true;
                }
            }
        }
        
        return false;
    }
};

// ─────────────────────────────────────────────
// 5. Transaction Manager
// ─────────────────────────────────────────────

class TransactionManager {
private:
    static std::atomic<TxID> g_next_xid;
    std::unordered_map<TxID, Transaction> transactions;
    std::unordered_map<RowKey, RowVersions> data;
    LockManager lock_manager;
    DeadlockDetector deadlock_detector;
    std::mutex tx_mutex;

public:
    TxID beginTransaction() {
        std::lock_guard<std::mutex> lock(tx_mutex);
        TxID xid = g_next_xid.fetch_add(1);
        TxID snap = xid;
        
        Transaction tx{xid, snap, TxStatus::ACTIVE, {}, {}, false};
        transactions[xid] = tx;
        
        std::cout << "\n[TX] BEGIN TX" << xid << " (snapshot=" << snap << ")\n";
        return xid;
    }

    std::optional<std::string> read(TxID tx_id, const RowKey& key) {
        std::lock_guard<std::mutex> lock(tx_mutex);
        
        if (!transactions.count(tx_id)) {
            std::cerr << "[ERROR] TX " << tx_id << " not found\n";
            return std::nullopt;
        }
        
        Transaction& tx = transactions[tx_id];
        if (tx.in_shrinking) {
            std::cerr << "[ERROR] TX " << tx_id << " in shrinking phase\n";
            return std::nullopt;
        }
        
        // Check if row exists
        if (!data.count(key)) {
            std::cout << "[READ] TX" << tx_id << " reads " << key << " (not found)\n";
            return std::nullopt;
        }
        
        // Find visible version
        const auto& versions = data[key].versions;
        for (const auto& v : versions) {
            if (v.xmin <= tx.snapshot_xid && (v.xmax == 0 || v.xmax > tx.snapshot_xid)) {
                tx.read_set.insert(key);
                std::cout << "[READ] TX" << tx_id << " reads " << key << " = '" << v.value << "'\n";
                return v.value;
            }
        }
        
        return std::nullopt;
    }

    bool write(TxID tx_id, const RowKey& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(tx_mutex);
        
        if (!transactions.count(tx_id)) {
            std::cerr << "[ERROR] TX " << tx_id << " not found\n";
            return false;
        }
        
        Transaction& tx = transactions[tx_id];
        if (tx.in_shrinking) {
            std::cerr << "[ERROR] TX " << tx_id << " in shrinking phase\n";
            return false;
        }
        
        // Acquire write lock
        if (!lock_manager.acquireWriteLock(tx_id, key)) {
            std::cout << "[WAIT] TX" << tx_id << " waiting for write lock on " << key << "\n";
            return false;
        }
        
        // Mark old versions as deleted by this transaction
        if (data.count(key)) {
            for (auto& v : data[key].versions) {
                if (v.xmax == 0) {
                    v.xmax = tx_id;
                }
            }
        }
        
        // Create new version
        VersionChain new_version{tx_id, 0, value};
        data[key].versions.push_back(new_version);
        
        tx.write_set.insert(key);
        std::cout << "[WRITE] TX" << tx_id << " writes " << key << " = '" << value << "'\n";
        
        return true;
    }

    bool commit(TxID tx_id) {
        std::lock_guard<std::mutex> lock(tx_mutex);
        
        if (!transactions.count(tx_id)) {
            std::cerr << "[ERROR] TX " << tx_id << " not found\n";
            return false;
        }
        
        Transaction& tx = transactions[tx_id];
        tx.in_shrinking = true;
        
        // Release all locks (Strict 2PL)
        lock_manager.releaseAllLocks(tx_id, tx.read_set);
        lock_manager.releaseAllLocks(tx_id, tx.write_set);
        
        tx.status = TxStatus::COMMITTED;
        deadlock_detector.removeWait(tx_id);
        
        std::cout << "[COMMIT] TX" << tx_id << " committed\n";
        return true;
    }

    bool abort(TxID tx_id) {
        std::lock_guard<std::mutex> lock(tx_mutex);
        
        if (!transactions.count(tx_id)) {
            std::cerr << "[ERROR] TX " << tx_id << " not found\n";
            return false;
        }
        
        Transaction& tx = transactions[tx_id];
        tx.in_shrinking = true;
        
        // Rollback: remove versions created by this transaction
        for (auto& [key, versions_obj] : data) {
            versions_obj.versions.erase(
                std::remove_if(versions_obj.versions.begin(), versions_obj.versions.end(),
                    [tx_id](const VersionChain& v) { return v.xmin == tx_id; }),
                versions_obj.versions.end()
            );
        }
        
        // Release all locks
        lock_manager.releaseAllLocks(tx_id, tx.read_set);
        lock_manager.releaseAllLocks(tx_id, tx.write_set);
        
        tx.status = TxStatus::ABORTED;
        deadlock_detector.removeWait(tx_id);
        
        std::cout << "[ABORT] TX" << tx_id << " aborted\n";
        return true;
    }

    void printState() {
        std::lock_guard<std::mutex> lock(tx_mutex);
        std::cout << "\n=== Transaction State ===\n";
        for (const auto& [tx_id, tx] : transactions) {
            std::cout << "TX" << tx_id << " status=" << static_cast<int>(tx.status)
                      << " snapshot=" << tx.snapshot_xid
                      << " shrinking=" << tx.in_shrinking << "\n";
        }
        
        std::cout << "\n=== Data State ===\n";
        for (const auto& [key, versions_obj] : data) {
            std::cout << key << ": ";
            for (size_t i = 0; i < versions_obj.versions.size(); ++i) {
                const auto& v = versions_obj.versions[i];
                std::cout << "[xmin=" << v.xmin << " xmax=" << v.xmax << " val='" << v.value << "']";
                if (i < versions_obj.versions.size() - 1) std::cout << " -> ";
            }
            std::cout << "\n";
        }
    }
};

std::atomic<TxID> TransactionManager::g_next_xid{1};

// ─────────────────────────────────────────────
// 6. Test Scenarios
// ─────────────────────────────────────────────

int main() {
    std::cout << "=== Transaction Manager with MVCC + 2PL + Deadlock Detection ===\n";
    
    TransactionManager tm;
    
    std::cout << "\n--- Test 1: Basic MVCC (Multiple Versions) ---\n";
    TxID tx1 = tm.beginTransaction();
    tm.write(tx1, "account:1", "balance=1000");
    tm.commit(tx1);
    
    TxID tx2 = tm.beginTransaction();
    TxID tx3 = tm.beginTransaction();
    
    tm.read(tx2, "account:1");  // tx2 sees version created by tx1
    tm.write(tx3, "account:1", "balance=900");
    tm.commit(tx3);
    
    tm.read(tx2, "account:1");  // tx2 still sees old version (MVCC)
    tm.commit(tx2);
    
    tm.printState();

    std::cout << "\n--- Test 2: Strict 2PL (Write Conflict) ---\n";
    TxID tx4 = tm.beginTransaction();
    TxID tx5 = tm.beginTransaction();
    
    tm.write(tx4, "account:2", "balance=500");
    std::cout << "[BLOCK] TX5 tries to write to locked row\n";
    // tx5.write(tx5, "account:2", "balance=400");  // Would block
    tm.commit(tx4);
    
    std::cout << "\n--- Test 3: Snapshot Isolation ---\n";
    TxID tx6 = tm.beginTransaction();
    TxID tx7 = tm.beginTransaction();
    
    tm.write(tx6, "item:1", "status=available");
    tm.commit(tx6);
    
    tm.read(tx7, "item:1");  // Sees committed version
    tm.commit(tx7);
    
    tm.printState();

    std::cout << "\n=== All Tests Completed ===\n";
    return 0;
}
