#include "transaction_manager.h"

#include <bits/stdc++.h>
using namespace std;

void MVCCStore::seed(const string& key, int value)
{
    rows[key].push_back(Version{value, 0, ++clock});
}

int MVCCStore::beginTimestamp() const
{
    return clock;
}

int MVCCStore::read(const string& key, const Transaction& tx) const
{
    const vector<Version>& chain = rows.at(key);

    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        if (it->commitTs <= tx.startTs)
            return it->value;
    }

    throw runtime_error("No committed version is visible for this transaction");
}

void MVCCStore::commit(Transaction& tx)
{
    int newCommitTime = ++clock;

    for (const auto& entry : tx.writes)
    {
        rows[entry.first].push_back(Version{entry.second, tx.id, newCommitTime});
    }

    tx.state = TxState::Committed;
}

void MVCCStore::printVersions() const
{
    cout << "\nStored MVCC versions\n";

    for (const auto& item : rows)
    {
        cout << item.first << ": ";

        for (const Version& ver : item.second)
        {
            cout << "[value=" << ver.value
                 << ", createdBy=T" << ver.createdBy
                 << ", commitTs=" << ver.commitTs << "] ";
        }

        cout << '\n';
    }
}

bool LockManager::acquire(Transaction& tx, const string& key, LockMode mode)
{
    if (tx.shrinking)
    {
        cout << "T" << tx.id << " cannot take more locks after releasing locks\n";
        tx.state = TxState::Aborted;
        return false;
    }

    Lock& currentLock = lockTable[key];
    set<int> blockingTransactions = blockersFor(tx.id, currentLock, mode);

    if (blockingTransactions.empty())
    {
        grant(tx, key, currentLock, mode);
        waitsFor.erase(tx.id);
        tx.state = TxState::Active;

        cout << "T" << tx.id << " acquired "
             << (mode == LockMode::Shared ? "shared" : "exclusive")
             << " lock on " << key << '\n';

        return true;
    }

    waitsFor[tx.id] = blockingTransactions;
    tx.state = TxState::Waiting;

    cout << "T" << tx.id << " is blocked by ";
    for (int id : blockingTransactions)
        cout << "T" << id << ' ';

    cout << "while requesting " << key << '\n';

    if (hasDeadlock())
    {
        cout << "Deadlock found. T" << tx.id << " is aborted\n";
        abort(tx);
    }

    return false;
}

void LockManager::releaseAll(Transaction& tx)
{
    tx.shrinking = true;

    for (const string& key : tx.sharedLocks)
    {
        lockTable[key].sharedOwners.erase(tx.id);
    }

    for (const string& key : tx.exclusiveLocks)
    {
        if (lockTable[key].exclusiveOwner == tx.id)
            lockTable[key].exclusiveOwner = -1;
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
    cout << "\nCurrent wait-for graph\n";

    if (waitsFor.empty())
    {
        cout << "empty\n";
        return;
    }

    for (const auto& relation : waitsFor)
    {
        cout << "T" << relation.first << " -> ";

        for (int id : relation.second)
            cout << "T" << id << ' ';

        cout << '\n';
    }
}

set<int> LockManager::blockersFor(int txId, const Lock& lock, LockMode mode) const
{
    set<int> result;

    if (lock.exclusiveOwner != -1 && lock.exclusiveOwner != txId)
        result.insert(lock.exclusiveOwner);

    if (mode == LockMode::Exclusive)
    {
        for (int owner : lock.sharedOwners)
        {
            if (owner != txId)
                result.insert(owner);
        }
    }

    return result;
}

void LockManager::grant(Transaction& tx, const string& key, Lock& lock, LockMode mode)
{
    if (mode == LockMode::Shared)
    {
        lock.sharedOwners.insert(tx.id);
        tx.sharedLocks.insert(key);
    }
    else
    {
        lock.sharedOwners.erase(tx.id);
        tx.sharedLocks.erase(key);

        lock.exclusiveOwner = tx.id;
        tx.exclusiveLocks.insert(key);
    }
}

bool LockManager::hasDeadlock() const
{
    set<int> visited;
    set<int> recursionStack;

    for (const auto& entry : waitsFor)
    {
        if (hasCycleFrom(entry.first, visited, recursionStack))
            return true;
    }

    return false;
}

bool LockManager::hasCycleFrom(int txId, set<int>& visited, set<int>& recursionStack) const
{
    if (recursionStack.count(txId))
        return true;

    if (visited.count(txId))
        return false;

    visited.insert(txId);
    recursionStack.insert(txId);

    auto pos = waitsFor.find(txId);

    if (pos != waitsFor.end())
    {
        for (int nextTx : pos->second)
        {
            if (hasCycleFrom(nextTx, visited, recursionStack))
                return true;
        }
    }

    recursionStack.erase(txId);
    return false;
}

void LockManager::removeFromWaitGraph(int txId)
{
    waitsFor.erase(txId);

    for (auto& entry : waitsFor)
        entry.second.erase(txId);

    for (auto it = waitsFor.begin(); it != waitsFor.end();)
    {
        if (it->second.empty())
            it = waitsFor.erase(it);
        else
            ++it;
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

    cout << "\nStarted T" << tx.id
         << " with snapshot timestamp " << tx.startTs << '\n';

    return tx;
}

void TransactionManager::read(Transaction& tx, const string& key)
{
    if (tx.state == TxState::Aborted)
        return;

    bool locked = locks.acquire(tx, key, LockMode::Shared);

    if (locked)
    {
        int value = store.read(key, tx);
        cout << "T" << tx.id << " read " << key << " = " << value << '\n';
    }
}

void TransactionManager::write(Transaction& tx, const string& key, int value)
{
    if (tx.state == TxState::Aborted)
        return;

    bool locked = locks.acquire(tx, key, LockMode::Exclusive);

    if (locked)
    {
        tx.writes.push_back({key, value});
        cout << "T" << tx.id << " staged write " << key << " = " << value << '\n';
    }
}

void TransactionManager::commit(Transaction& tx)
{
    if (tx.state == TxState::Aborted)
    {
        cout << "T" << tx.id << " cannot commit because it is aborted\n";
        return;
    }

    store.commit(tx);
    locks.releaseAll(tx);

    cout << "T" << tx.id << " committed successfully\n";
}

void TransactionManager::show() const
{
    locks.printWaitGraph();
    store.printVersions();
}