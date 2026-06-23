#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <sstream>

// ─────────────────────────────────────────────
// 1. Transaction State & Metadata
// ─────────────────────────────────────────────

using TxID = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID id;
    TxID snapshot_xid; // For MVCC: Read snapshot, see commits < snapshot_xid
    TxStatus status = TxStatus::ACTIVE;
    bool in_shrinking = false; // 2PL Phase flag
};

// Global transaction state
static std::atomic<TxID> g_next_xid{1};
static std::mutex g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

// Begin a transaction
TxID begin_transaction() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    // MVCC snapshot is the current state of XIDs
    TxID snap = xid;
    g_transactions[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

// ─────────────────────────────────────────────
// 2. Deadlock Detection (Waits-For Graph)
// ─────────────────────────────────────────────

static std::mutex g_lock_mgr_mutex;
// Key -> TxID holding the exclusive lock
static std::unordered_map<RowKey, TxID> g_locks;
// TxID -> TxID (waiting for)
static std::unordered_map<TxID, TxID> g_waits_for;

bool check_cycle(TxID start, TxID current, std::unordered_set<TxID>& visited) {
    if (current == start && !visited.empty()) return true;
    if (visited.count(current)) return false;
    
    visited.insert(current);
    if (g_waits_for.count(current)) {
        return check_cycle(start, g_waits_for[current], visited);
    }
    return false;
}

bool would_deadlock(TxID xid, TxID holding_xid) {
    g_waits_for[xid] = holding_xid;
    std::unordered_set<TxID> visited;
    bool cycle = check_cycle(xid, xid, visited);
    g_waits_for.erase(xid); // Clean up immediately, just checking
    return cycle;
}

// ─────────────────────────────────────────────
// 3. Strict 2PL & Lock Management
// ─────────────────────────────────────────────

// Request exclusive lock on a row
void acquire_lock(TxID xid, const RowKey& key) {
    while (true) {
        std::unique_lock<std::mutex> lk(g_lock_mgr_mutex);
        
        {
            std::lock_guard<std::mutex> tx_lk(g_tx_mutex);
            if (g_transactions.at(xid).in_shrinking) {
                throw std::runtime_error("2PL Violation: Cannot acquire lock in shrinking phase");
            }
        }

        if (g_locks.count(key) == 0 || g_locks[key] == xid) {
            // Lock available or already held
            g_locks[key] = xid;
            return;
        }

        TxID holder = g_locks[key];
        
        // If holder is completed, clean up lock
        if (is_committed(holder) || is_aborted(holder)) {
            g_locks[key] = xid;
            return;
        }

        if (would_deadlock(xid, holder)) {
            throw std::runtime_error("Deadlock detected! Aborting transaction " + std::to_string(xid));
        }

        // Add to waits-for graph while waiting
        g_waits_for[xid] = holder;
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // simple backoff
        
        lk.lock();
        g_waits_for.erase(xid);
    }
}

// Release locks at commit/abort (Strict 2PL)
void release_locks(TxID xid) {
    std::lock_guard<std::mutex> lk(g_lock_mgr_mutex);
    for (auto it = g_locks.begin(); it != g_locks.end(); ) {
        if (it->second == xid) {
            it = g_locks.erase(it);
        } else {
            ++it;
        }
    }
}

// ─────────────────────────────────────────────
// 4. MVCC Version Chains
// ─────────────────────────────────────────────

struct RowVersion {
    std::string value;
    TxID xmin; // created by
    TxID xmax; // deleted/updated by (0 = still live)
};

static std::mutex g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // xmin must be committed and <= snapshot OR created by reader itself
    bool xmin_ok = (v.xmin == reader_xid) || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    // xmax must not have been deleted before our snapshot
    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_xid) || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

std::optional<std::string> mvcc_read(TxID xid, const RowKey& key) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;

    // Traverse version chain (newest first)
    for (const auto& v : it->second) {
        if (is_visible(v, snap, xid)) {
            return v.value;
        }
    }
    return std::nullopt;
}

void mvcc_write(TxID xid, const RowKey& key, const std::string& value) {
    acquire_lock(xid, key); // 2PL execution

    std::lock_guard<std::mutex> lk(g_heap_mutex);
    
    // Invalidate old version
    if (g_heap.count(key) > 0) {
        for (auto& v : g_heap[key]) {
            if (v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }

    // Insert new version at the head of the chain
    g_heap[key].push_front({value, xid, 0});
}

// ─────────────────────────────────────────────
// 5. Commit & Abort 
// ─────────────────────────────────────────────

void commit_transaction(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        g_transactions[xid].in_shrinking = true; // Enter shrinking phase
        g_transactions[xid].status = TxStatus::COMMITTED;
    }
    release_locks(xid); // Release all locks strictly at the end
    std::cout << "Transaction " << xid << " COMMITTED.\n";
}

void abort_transaction(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        g_transactions[xid].in_shrinking = true;
        g_transactions[xid].status = TxStatus::ABORTED;
    }
    
    // Undo writes: remove versions created by this xid, reset xmax
    std::lock_guard<std::mutex> heap_lk(g_heap_mutex);
    for (auto& [key, chain] : g_heap) {
        chain.remove_if([xid](const RowVersion& v) { return v.xmin == xid; });
        for (auto& v : chain) {
            if (v.xmax == xid) v.xmax = 0;
        }
    }

    release_locks(xid);
    std::cout << "Transaction " << xid << " ABORTED (Rolled back).\n";
}

// ─────────────────────────────────────────────
// 6. Testing
// ─────────────────────────────────────────────

int main() {
    TxID t1 = begin_transaction();
    mvcc_write(t1, "A", "Apple");
    commit_transaction(t1);

    TxID t2 = begin_transaction();
    TxID t3 = begin_transaction();

    // T2 updates A
    mvcc_write(t2, "A", "Apricot");

    // T3 reads A (MVCC prevents blocking, should see 'Apple')
    auto val = mvcc_read(t3, "A");
    std::cout << "T3 reads A: " << (val ? *val : "NULL") << " (Expected: Apple)\n";

    commit_transaction(t2);

    // Even though T2 committed, T3 still sees snapshot state 'Apple'
    val = mvcc_read(t3, "A");
    std::cout << "T3 reads A after T2 commit: " << (val ? *val : "NULL") << " (Expected: Apple)\n";
    commit_transaction(t3);

    // T4 starts after T2 commit, so it sees 'Apricot'
    TxID t4 = begin_transaction();
    val = mvcc_read(t4, "A");
    std::cout << "T4 reads A: " << (val ? *val : "NULL") << " (Expected: Apricot)\n";
    commit_transaction(t4);

    return 0;
}
