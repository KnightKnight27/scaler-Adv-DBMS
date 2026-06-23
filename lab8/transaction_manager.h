#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include <bits/stdc++.h>
enum class TxState
{
    Active,
    Waiting,
    Aborted,
    Committed
};

enum class LockMode
{
    Shared,
    Exclusive
};

struct Version
{
    int value;
    int createdBy;
    int commitTs;
};

struct Transaction
{
    int id = 0;
    int startTs = 0;
    TxState state = TxState::Active;
    bool hasReleasedLock = false;

    std::set<std::string> sharedLocks;
    std::set<std::string> exclusiveLocks;
    std::vector<std::pair<std::string, int>> pendingWrites;
};

class MVCCStore
{
public:
    void seed(const std::string& key, int value);
    int beginTimestamp() const;
    int read(const std::string& key, const Transaction& tx) const;
    void commit(Transaction& tx);
    void printVersions() const;

private:
    std::map<std::string, std::vector<Version>> data;
    int currentTime = 0;
};

class LockManager
{
public:
    bool acquire(Transaction& tx, const std::string& key, LockMode mode);
    void releaseAll(Transaction& tx);
    void abort(Transaction& tx);
    void printWaitGraph() const;

private:
    struct LockEntry
    {
        std::set<int> readers;
        int writer = -1;
    };

    std::map<std::string, LockEntry> locks;
    std::map<int, std::set<int>> waitGraph;

    std::set<int> getBlockers(int txId, const LockEntry& entry, LockMode mode) const;
    void giveLock(Transaction& tx, const std::string& key, LockEntry& entry, LockMode mode);
    bool deadlockExists() const;
    bool dfsCycle(int txId, std::set<int>& visited, std::set<int>& path) const;
    void removeTransactionFromGraph(int txId);
};

class TransactionManager
{
public:
    TransactionManager();

    Transaction begin();
    void read(Transaction& tx, const std::string& key);
    void write(Transaction& tx, const std::string& key, int value);
    void commit(Transaction& tx);
    void show() const;

private:
    MVCCStore store;
    LockManager lockManager;
    int nextId = 1;
};

#endif