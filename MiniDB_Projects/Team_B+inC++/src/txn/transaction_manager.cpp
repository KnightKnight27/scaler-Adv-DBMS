#include "transaction_manager.hpp"

#include <functional>
#include <limits>

DeadlockException::DeadlockException(TxID xid)
    : std::runtime_error("deadlock detected; aborting transaction " + std::to_string(xid)) {}

TransactionManager::TransactionManager(ConcurrencyMode mode) : mode_(mode) {}

TxID TransactionManager::begin() {
    std::lock_guard<std::mutex> lk(tx_mu_);
    TxID xid = next_xid_++;
    // snapshot = own id
    txns_[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool TransactionManager::is_committed(TxID xid) {
    if (xid == 0) return true;  // genesis
    std::lock_guard<std::mutex> lk(tx_mu_);
    auto it = txns_.find(xid);
    return it != txns_.end() && it->second.status == TxStatus::COMMITTED;
}

// visible if created by someone we see and not deleted by someone we see
bool TransactionManager::is_visible(const RowVersion& v, TxID snapshot, TxID reader) {
    bool xmin_visible = (v.xmin == reader) || (is_committed(v.xmin) && v.xmin < snapshot);
    if (!xmin_visible) return false;
    if (v.xmax == 0) return true;                              // never deleted
    bool xmax_visible = (v.xmax == reader) || (is_committed(v.xmax) && v.xmax < snapshot);
    return !xmax_visible;
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey& key) {
    if (mode_ == ConcurrencyMode::TWO_PL)
        acquire_lock(xid, key, LockMode::SHARED);  // 2PL readers block on writers

    TxID snapshot;
    if (mode_ == ConcurrencyMode::MVCC) {
        std::lock_guard<std::mutex> lk(tx_mu_);
        snapshot = txns_.at(xid).snapshot;
    } else {
        snapshot = std::numeric_limits<TxID>::max();  // 2PL: latest committed
    }

    std::lock_guard<std::mutex> lk(heap_mu_);
    auto it = heap_.find(key);
    if (it == heap_.end()) return std::nullopt;
    for (const RowVersion& v : it->second)
        if (is_visible(v, snapshot, xid)) return v.value;
    return std::nullopt;
}

void TransactionManager::write(TxID xid, const RowKey& key, const std::string& value) {
    acquire_lock(xid, key, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> lk(heap_mu_);
    std::list<RowVersion>& chain = heap_[key];
    for (RowVersion& v : chain)             // supersede the live version
        if (v.xmax == 0) { v.xmax = xid; break; }
    chain.push_front({value, xid, 0});      // fresh version, xmin = us
}

void TransactionManager::erase(TxID xid, const RowKey& key) {
    acquire_lock(xid, key, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> lk(heap_mu_);
    auto it = heap_.find(key);
    if (it == heap_.end()) return;
    for (RowVersion& v : it->second)
        if (v.xmax == 0) { v.xmax = xid; return; }  // tombstone live version
}

void TransactionManager::commit(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        txns_.at(xid).status = TxStatus::COMMITTED;
    }
    release_locks(xid);
}

void TransactionManager::abort(TxID xid) {
    // aborted xid never commits -> its versions just become invisible
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        txns_.at(xid).status = TxStatus::ABORTED;
    }
    release_locks(xid);
}

void TransactionManager::load_committed(const RowKey& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(heap_mu_);
    heap_[key].push_front({value, /*xmin=genesis*/ 0, /*xmax=*/0});
}

std::vector<std::pair<RowKey, std::string>> TransactionManager::snapshot_scan(TxID xid) {
    TxID snapshot;
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        snapshot = (mode_ == ConcurrencyMode::MVCC) ? txns_.at(xid).snapshot
                                                    : std::numeric_limits<TxID>::max();
    }
    std::vector<std::pair<RowKey, std::string>> out;
    std::lock_guard<std::mutex> lk(heap_mu_);
    for (auto& [key, chain] : heap_)
        for (const RowVersion& v : chain)
            if (is_visible(v, snapshot, xid)) { out.emplace_back(key, v.value); break; }
    return out;
}

// lock manager: strict 2PL + waits-for deadlock detection
void TransactionManager::acquire_lock(TxID xid, const RowKey& key, LockMode mode) {
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        if (txns_.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: lock requested in shrinking phase");
    }

    std::unique_lock<std::mutex> lk(lm_mu_);
    std::list<LockRequest>& q = lock_table_[key];

    // already hold a sufficient lock?
    for (LockRequest& r : q)
        if (r.xid == xid && r.granted && (mode == LockMode::SHARED || r.mode == LockMode::EXCLUSIVE))
            return;

    q.push_back({xid, mode, false});

    while (true) {
        std::unordered_set<TxID> holders;
        for (LockRequest& r : q) {
            if (r.xid == xid || !r.granted) continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) holders.insert(r.xid);
        }
        if (holders.empty()) {                       // no conflict -> grant
            for (LockRequest& r : q) if (r.xid == xid) r.granted = true;
            waits_for_.erase(xid);
            return;
        }
        waits_for_[xid] = holders;                   // waiter -> holders it blocks on
        if (has_cycle(xid)) {
            q.remove_if([&](const LockRequest& r) { return r.xid == xid && !r.granted; });
            waits_for_.erase(xid);
            throw DeadlockException(xid);
        }
        lm_cv_.wait(lk);                             // woken on commit/abort
    }
}

void TransactionManager::release_locks(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        txns_.at(xid).in_shrinking = true;  // shrinking phase begins
    }
    std::lock_guard<std::mutex> lk(lm_mu_);
    for (auto& [key, q] : lock_table_)
        q.remove_if([&](const LockRequest& r) { return r.xid == xid; });
    waits_for_.erase(xid);
    lm_cv_.notify_all();  // wake waiters
}

// DFS for a cycle in the waits-for graph. lm_mu_ held.
bool TransactionManager::has_cycle(TxID start) {
    std::unordered_set<TxID> on_stack, visited;
    std::function<bool(TxID)> dfs = [&](TxID node) {
        visited.insert(node);
        on_stack.insert(node);
        auto it = waits_for_.find(node);
        if (it != waits_for_.end())
            for (TxID nb : it->second) {
                if (on_stack.count(nb)) return true;        // back-edge -> cycle
                if (!visited.count(nb) && dfs(nb)) return true;
            }
        on_stack.erase(node);
        return false;
    };
    return dfs(start);
}
