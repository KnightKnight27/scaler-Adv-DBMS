#include "transaction_manager.h"
#include <iostream>

// CycleDetectedException implementation
CycleDetectedException::CycleDetectedException(TxnID txId) 
    : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(txId)) {}

bool DbTransactionEngine::checkCommitted(TxnID txId)
{
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txnMap_.find(txId);
    return it != txnMap_.end() && it->second.state == TransactionState::SUCCESS;
}

bool DbTransactionEngine::checkAborted(TxnID txId)
{
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txnMap_.find(txId);
    return it != txnMap_.end() && it->second.state == TransactionState::ROLLED_BACK;
}

bool DbTransactionEngine::checkVisibility(const DbVersion &v, TxnID snapId, TxnID readerId)
{
    // createdTx must be committed and < snapId, OR it must be our own write
    bool createdOk = (v.createdTx == readerId) || (checkCommitted(v.createdTx) && v.createdTx < snapId);
    if (!createdOk)
        return false;

    // deletedTx: version must not have been deleted before our snapshot
    if (v.deletedTx == 0)
        return true;
    bool deletedInvisible = (v.deletedTx == readerId) || (checkCommitted(v.deletedTx) && v.deletedTx < snapId);
    return !deletedInvisible;
}

bool DbTransactionEngine::detectCycle(TxnID start, const std::unordered_map<TxnID, std::unordered_set<TxnID>> &graph)
{
    std::unordered_set<TxnID> visited, activeStack;
    std::function<bool(TxnID)> dfs = [&](TxnID node) -> bool
    {
        visited.insert(node);
        activeStack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end())
        {
            for (TxnID neighbor : it->second)
            {
                if (!visited.count(neighbor) && dfs(neighbor))
                    return true;
                if (activeStack.count(neighbor))
                    return true;
            }
        }
        activeStack.erase(node);
        return false;
    };
    return dfs(start);
}

// DbTransactionEngine Public Methods
TxnID DbTransactionEngine::startTransaction()
{
    std::lock_guard<std::mutex> lock(txnMutex_);
    TxnID newId = nextTxId_.fetch_add(1);
    TxnID snap = newId;
    txnMap_[newId] = TxContext{newId, snap, TransactionState::RUNNING, false};
    std::cout << "[TX " << newId << "] BEGIN\n";
    return newId;
}

std::optional<std::string> DbTransactionEngine::readKey(TxnID txId, const DbKey &key)
{
    requestLock(key, txId, LockType::READ_LOCK);
    std::lock_guard<std::mutex> lock(storageMutex_);
    TxnID snap;
    {
        std::lock_guard<std::mutex> txnLock(txnMutex_);
        snap = txnMap_.at(txId).snapId;
    }

    auto it = dbStorage_.find(key);
    if (it == dbStorage_.end())
        return std::nullopt;

    for (auto &v : it->second)
    {
        if (checkVisibility(v, snap, txId))
        {
            return v.content;
        }
    }
    return std::nullopt;
}

void DbTransactionEngine::insertKey(TxnID txId, const DbKey &key, const std::string &value)
{
    requestLock(key, txId, LockType::WRITE_LOCK);
    std::lock_guard<std::mutex> lock(storageMutex_);
    dbStorage_[key].push_front({value, txId, 0});
}

void DbTransactionEngine::updateKey(TxnID txId, const DbKey &key, const std::string &value)
{
    requestLock(key, txId, LockType::WRITE_LOCK);
    std::lock_guard<std::mutex> lock(storageMutex_);
    TxnID snap;
    {
        std::lock_guard<std::mutex> txnLock(txnMutex_);
        snap = txnMap_.at(txId).snapId;
    }

    auto it = dbStorage_.find(key);
    if (it != dbStorage_.end())
    {
        for (auto &v : it->second)
        {
            if (checkVisibility(v, snap, txId) && v.deletedTx == 0)
            {
                v.deletedTx = txId; // logically delete old version
                break;
            }
        }
    }
    dbStorage_[key].push_front({value, txId, 0});
}

void DbTransactionEngine::deleteKey(TxnID txId, const DbKey &key)
{
    requestLock(key, txId, LockType::WRITE_LOCK);
    std::lock_guard<std::mutex> lock(storageMutex_);
    TxnID snap;
    {
        std::lock_guard<std::mutex> txnLock(txnMutex_);
        snap = txnMap_.at(txId).snapId;
    }
    auto it = dbStorage_.find(key);
    if (it == dbStorage_.end())
        return;
    for (auto &v : it->second)
    {
        if (checkVisibility(v, snap, txId) && v.deletedTx == 0)
        {
            v.deletedTx = txId;
            return;
        }
    }
}

void DbTransactionEngine::commitTransaction(TxnID txId)
{
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        txnMap_.at(txId).state = TransactionState::SUCCESS;
    }
    releaseAllLocks(txId);
    std::cout << "[TX " << txId << "] COMMITTED\n";
}

void DbTransactionEngine::rollbackTransaction(TxnID txId)
{
    // Roll back
    {
        std::lock_guard<std::mutex> lock(storageMutex_);
        for (auto &pair : dbStorage_)
        {
            auto &chain = pair.second;
            for (auto &v : chain)
            {
                if (v.createdTx == txId)
                    v.deletedTx = txId; // make own inserts invisible
                if (v.deletedTx == txId)
                    v.deletedTx = 0; // undo own deletes
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        txnMap_.at(txId).state = TransactionState::ROLLED_BACK;
    }
    releaseAllLocks(txId);
    std::cout << "[TX " << txId << "] ABORTED\n";
}

void DbTransactionEngine::requestLock(const DbKey &key, TxnID txId, LockType mode)
{
    // 2PL: cannot acquire lock in shrinking phase
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        if (txnMap_.at(txId).shrinkingPhase)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    LockWaitQueue *lq_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(lmMutex_);
        lq_ptr = &lockTable_[key];
    }
    LockWaitQueue &lq = *lq_ptr;
    std::unique_lock<std::mutex> ul(lq.mtx);

    for (auto &r : lq.reqs)
    {
        if (r.txnId == txId && r.isGranted)
        {
            if (mode == LockType::READ_LOCK)
                return; // already have shared (or better)
            if (r.lockMode == LockType::WRITE_LOCK)
                return; // already exclusive
        }
    }
    lq.reqs.push_back({txId, mode, false});
    auto &my_req = lq.reqs.back();

    while (true)
    {
        bool conflict = false;
        std::unordered_set<TxnID> blocking;
        for (auto &r : lq.reqs)
        {
            if (&r == &my_req)
                break;
            if (!r.isGranted)
                continue;
            if (mode == LockType::WRITE_LOCK || r.lockMode == LockType::WRITE_LOCK)
            {
                if (r.txnId != txId)
                {
                    conflict = true;
                    blocking.insert(r.txnId);
                }
            }
        }
        if (!conflict)
        {
            my_req.isGranted = true;
            {
                std::lock_guard<std::mutex> lock(lmMutex_);
                dependencyGraph_.erase(txId);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(lmMutex_);
            dependencyGraph_[txId] = blocking;
            if (detectCycle(txId, dependencyGraph_))
            {
                dependencyGraph_.erase(txId);
                lq.reqs.remove_if([&](const LockReq &r)
                                  { return r.txnId == txId && !r.isGranted; });
                throw CycleDetectedException(txId);
            }
        }

        lq.cvWait.wait(ul);
    }
}

void DbTransactionEngine::releaseAllLocks(TxnID txId)
{
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        if (txnMap_.count(txId))
            txnMap_.at(txId).shrinkingPhase = true;
    }
    std::vector<DbKey> keys;
    {
        std::lock_guard<std::mutex> lock(lmMutex_);
        for (auto &pair : lockTable_)
        {
            keys.push_back(pair.first);
        }
    }
    for (const auto &key : keys)
    {
        LockWaitQueue *lq_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(lmMutex_);
            lq_ptr = &lockTable_[key];
        }
        std::unique_lock<std::mutex> ul(lq_ptr->mtx);
        lq_ptr->reqs.remove_if([&](const LockReq &r)
                               { return r.txnId == txId; });
        lq_ptr->cvWait.notify_all();
    }

    {
        std::lock_guard<std::mutex> lock(lmMutex_);
        dependencyGraph_.erase(txId);
    }
}