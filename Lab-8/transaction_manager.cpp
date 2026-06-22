#include "transaction_manager.h"
#include <iostream>

//  DeadlockException implementation
DeadlockException::DeadlockException(TxID xid) : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}

bool TransactionManager::is_committed(TxID xid)
{
    std::lock_guard<std::mutex> lk(tx_mutex_);
    auto it = transactions_.find(xid);
    return it != transactions_.end() && it->second.status == TxStatus::COMMITTED;
}

bool TransactionManager::is_aborted(TxID xid)
{
    std::lock_guard<std::mutex> lk(tx_mutex_);
    auto it = transactions_.find(xid);
    return it != transactions_.end() && it->second.status == TxStatus::ABORTED;
}

bool TransactionManager::is_visible(const RowVersion &v, TxID snapshot_xid, TxID reader_xid)
{
    // xmin must be committed and <= snapshot, OR it must be our own write
    bool xmin_ok = (v.xmin == reader_xid) || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok)
        return false;

    // xmax: version must not have been deleted before our snapshot
    if (v.xmax == 0)
        return true;
    bool xmax_invisible = (v.xmax == reader_xid) || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

bool TransactionManager::has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>> &graph)
{
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool
    {
        visited.insert(node);
        stack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end())
        {
            for (TxID nb : it->second)
            {
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

//  TransactionManager Public Methods
TxID TransactionManager::begin()
{
    std::lock_guard<std::mutex> lk(tx_mutex_);
    TxID xid = next_xid_.fetch_add(1);
    TxID snap = xid;
    transactions_[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    std::cout << "[TX " << xid << "] BEGIN\n";
    return xid;
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey &key)
{
    acquire_lock(key, xid, LockMode::SHARED);
    std::lock_guard<std::mutex> lk(heap_mutex_);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(tx_mutex_);
        snap = transactions_.at(xid).snapshot_xid;
    }

    auto it = heap_.find(key);
    if (it == heap_.end())
        return std::nullopt;

    for (auto &v : it->second)
    {
        if (is_visible(v, snap, xid))
        {
            return v.value;
        }
    }
    return std::nullopt;
}

void TransactionManager::insert(TxID xid, const RowKey &key, const std::string &value)
{
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> lk(heap_mutex_);
    heap_[key].push_front({value, xid, 0});
}

void TransactionManager::update(TxID xid, const RowKey &key, const std::string &value)
{
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> lk(heap_mutex_);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(tx_mutex_);
        snap = transactions_.at(xid).snapshot_xid;
    }

    auto it = heap_.find(key);
    if (it != heap_.end())
    {
        for (auto &v : it->second)
        {
            if (is_visible(v, snap, xid) && v.xmax == 0)
            {
                v.xmax = xid; // logically delete old version
                break;
            }
        }
    }
    heap_[key].push_front({value, xid, 0});
}

void TransactionManager::remove(TxID xid, const RowKey &key)
{
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> lk(heap_mutex_);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(tx_mutex_);
        snap = transactions_.at(xid).snapshot_xid;
    }
    auto it = heap_.find(key);
    if (it == heap_.end())
        return;
    for (auto &v : it->second)
    {
        if (is_visible(v, snap, xid) && v.xmax == 0)
        {
            v.xmax = xid;
            return;
        }
    }
}

void TransactionManager::commit(TxID xid)
{
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        transactions_.at(xid).status = TxStatus::COMMITTED;
    }
    release_locks(xid);
    std::cout << "[TX " << xid << "] COMMITTED\n";
}

void TransactionManager::abort(TxID xid)
{
    // Roll back
    {
        std::lock_guard<std::mutex> lk(heap_mutex_);
        for (auto &pair : heap_)
        {
            auto &chain = pair.second;
            for (auto &v : chain)
            {
                if (v.xmin == xid)
                    v.xmax = xid; // make own inserts invisible
                if (v.xmax == xid)
                    v.xmax = 0; // undo own deletes
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        transactions_.at(xid).status = TxStatus::ABORTED;
    }
    release_locks(xid);
    std::cout << "[TX " << xid << "] ABORTED\n";
}

void TransactionManager::acquire_lock(const RowKey &key, TxID xid, LockMode mode)
{
    // 2PL: cannot acquire lock in shrinking phase
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (transactions_.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    LockQueue *lq_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lk(lm_mutex_);
        lq_ptr = &lock_table_[key];
    }
    LockQueue &lq = *lq_ptr;
    std::unique_lock<std::mutex> ul(lq.mu);

    for (auto &r : lq.requests)
    {
        if (r.xid == xid && r.granted)
        {
            if (mode == LockMode::SHARED)
                return; // already have shared (or better)
            if (r.mode == LockMode::EXCLUSIVE)
                return; // already exclusive
        }
    }
    lq.requests.push_back({xid, mode, false});
    auto &my_req = lq.requests.back();

    while (true)
    {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto &r : lq.requests)
        {
            if (&r == &my_req)
                break;
            if (!r.granted)
                continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE)
            {
                if (r.xid != xid)
                {
                    conflict = true;
                    blocking.insert(r.xid);
                }
            }
        }
        if (!conflict)
        {
            my_req.granted = true;
            {
                std::lock_guard<std::mutex> lk(lm_mutex_);
                waits_for_.erase(xid);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lk(lm_mutex_);
            waits_for_[xid] = blocking;
            if (has_cycle(xid, waits_for_))
            {
                waits_for_.erase(xid);
                lq.requests.remove_if([&](const LockRequest &r)
                                      { return r.xid == xid && !r.granted; });
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);
    }
}

void TransactionManager::release_locks(TxID xid)
{
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        if (transactions_.count(xid))
            transactions_.at(xid).in_shrinking = true;
    }
    std::vector<RowKey> keys;
    {
        std::lock_guard<std::mutex> lk(lm_mutex_);
        for (auto &pair : lock_table_)
        {
            keys.push_back(pair.first);
        }
    }
    for (const auto &key : keys)
    {
        LockQueue *lq_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lk(lm_mutex_);
            lq_ptr = &lock_table_[key];
        }
        std::unique_lock<std::mutex> ul(lq_ptr->mu);
        lq_ptr->requests.remove_if([&](const LockRequest &r)
                                   { return r.xid == xid; });
        lq_ptr->cv.notify_all();
    }

    {
        std::lock_guard<std::mutex> lk(lm_mutex_);
        waits_for_.erase(xid);
    }
}
