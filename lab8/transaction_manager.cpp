#include "transaction_manager.h"

#include <iostream>
#include <stdexcept>

using namespace std;

void MVCCStore::seed(const string& key, int value)
{
    rows[key].push_back({value, 0, ++clock});
}

int MVCCStore::beginTimestamp() const
{
    return clock;
}

int MVCCStore::read(const string& key, const Transaction& tx) const
{
    const auto& versions = rows.at(key);

    for(auto it = versions.rbegin(); it != versions.rend(); ++it)
    {
        if(it->commitTs <= tx.startTs)
        {
            return it->value;
        }
    }

    throw runtime_error("No visible committed version found");
}

void MVCCStore::commit(Transaction& tx)
{
    int commitTs = ++clock;

    for(const auto& write : tx.writes)
    {
        rows[write.first].push_back({write.second, tx.id, commitTs});
    }

    tx.state = TxState::Committed;
}

void MVCCStore::printVersions() const
{
    cout << "\nMVCC version chains\n";

    for(const auto& row : rows)
    {
        cout << row.first << ": ";

        for(const Version& version : row.second)
        {
            cout << "[value=" << version.value
                 << ", tx=" << version.createdBy
                 << ", commitTs=" << version.commitTs << "] ";
        }

        cout << '\n';
    }
}

bool LockManager::acquire(Transaction& tx, const string& key, LockMode mode)
{
    if(tx.shrinking)
    {
        cout << "T" << tx.id << " cannot acquire a new lock after releasing one\n";
        tx.state = TxState::Aborted;
        return false;
    }

    Lock& lock = lockTable[key];
    set<int> blockers = blockersFor(tx.id, lock, mode);

    if(blockers.empty())
    {
        grant(tx, key, lock, mode);
        waitsFor.erase(tx.id);
        tx.state = TxState::Active;

        cout << "T" << tx.id << " granted "
             << (mode == LockMode::Shared ? "S" : "X")
             << "-lock on " << key << '\n';
        return true;
    }

    waitsFor[tx.id] = blockers;
    tx.state = TxState::Waiting;

    cout << "T" << tx.id << " waits for ";
    for(int blocker : blockers)
    {
        cout << "T" << blocker << ' ';
    }
    cout << "on " << key << '\n';

    if(hasDeadlock())
    {
        cout << "Deadlock detected. Aborting T" << tx.id << '\n';
        abort(tx);
    }

    return false;
}

void LockManager::releaseAll(Transaction& tx)
{
    tx.shrinking = true;

    for(const string& key : tx.sharedLocks)
    {
        lockTable[key].sharedOwners.erase(tx.id);
    }

    for(const string& key : tx.exclusiveLocks)
    {
        if(lockTable[key].exclusiveOwner == tx.id)
        {
            lockTable[key].exclusiveOwner = -1;
        }
    }

    tx.sharedLocks.clear();
    tx.exclusiveLocks.clear();
    removeFromWaitGraph(tx.id);
}

void LockManager::abort(Transaction& tx)
{
    tx.writes.clear();
    tx.state = TxState::Aborted;
    releaseAll(tx);
}

void LockManager::printWaitGraph() const
{
    cout << "\nWait-for graph\n";

    if(waitsFor.empty())
    {
        cout << "empty\n";
        return;
    }

    for(const auto& edge : waitsFor)
    {
        cout << "T" << edge.first << " -> ";

        for(int txId : edge.second)
        {
            cout << "T" << txId << ' ';
        }

        cout << '\n';
    }
}

set<int> LockManager::blockersFor(int txId, const Lock& lock, LockMode mode) const
{
    set<int> blockers;

    if(lock.exclusiveOwner != -1 && lock.exclusiveOwner != txId)
    {
        blockers.insert(lock.exclusiveOwner);
    }

    if(mode == LockMode::Exclusive)
    {
        for(int owner : lock.sharedOwners)
        {
            if(owner != txId)
            {
                blockers.insert(owner);
            }
        }
    }

    return blockers;
}

void LockManager::grant(Transaction& tx, const string& key, Lock& lock, LockMode mode)
{
    if(mode == LockMode::Shared)
    {
        lock.sharedOwners.insert(tx.id);
        tx.sharedLocks.insert(key);
        return;
    }

    lock.sharedOwners.erase(tx.id);
    tx.sharedLocks.erase(key);
    lock.exclusiveOwner = tx.id;
    tx.exclusiveLocks.insert(key);
}

bool LockManager::hasDeadlock() const
{
    set<int> seen;
    set<int> activePath;

    for(const auto& node : waitsFor)
    {
        if(hasCycleFrom(node.first, seen, activePath))
        {
            return true;
        }
    }

    return false;
}

bool LockManager::hasCycleFrom(int txId, set<int>& seen, set<int>& activePath) const
{
    if(activePath.count(txId) > 0)
    {
        return true;
    }

    if(seen.count(txId) > 0)
    {
        return false;
    }

    seen.insert(txId);
    activePath.insert(txId);

    auto it = waitsFor.find(txId);
    if(it != waitsFor.end())
    {
        for(int next : it->second)
        {
            if(hasCycleFrom(next, seen, activePath))
            {
                return true;
            }
        }
    }

    activePath.erase(txId);
    return false;
}

void LockManager::removeFromWaitGraph(int txId)
{
    waitsFor.erase(txId);

    for(auto& edge : waitsFor)
    {
        edge.second.erase(txId);
    }

    for(auto it = waitsFor.begin(); it != waitsFor.end();)
    {
        if(it->second.empty())
        {
            it = waitsFor.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

TransactionManager::TransactionManager()
{
    store.seed("A", 100);
    store.seed("B", 200);
}

Transaction TransactionManager::begin()
{
    Transaction tx;
    tx.id = nextTxId++;
    tx.startTs = store.beginTimestamp();

    cout << "\nBegin T" << tx.id << " at snapshot " << tx.startTs << '\n';
    return tx;
}

void TransactionManager::read(Transaction& tx, const string& key)
{
    if(tx.state == TxState::Aborted)
    {
        return;
    }

    if(locks.acquire(tx, key, LockMode::Shared))
    {
        cout << "T" << tx.id << " reads " << key << " = " << store.read(key, tx) << '\n';
    }
}

void TransactionManager::write(Transaction& tx, const string& key, int value)
{
    if(tx.state == TxState::Aborted)
    {
        return;
    }

    if(locks.acquire(tx, key, LockMode::Exclusive))
    {
        tx.writes.push_back({key, value});
        cout << "T" << tx.id << " buffers " << key << " = " << value << '\n';
    }
}

void TransactionManager::commit(Transaction& tx)
{
    if(tx.state == TxState::Aborted)
    {
        cout << "T" << tx.id << " cannot commit because it was aborted\n";
        return;
    }

    store.commit(tx);
    locks.releaseAll(tx);
    cout << "T" << tx.id << " committed\n";
}

void TransactionManager::show() const
{
    locks.printWaitGraph();
    store.printVersions();
}