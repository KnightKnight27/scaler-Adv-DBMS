#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include <map>
#include <set>
#include <string>
#include <vector>

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
    bool shrinking = false;
    std::set<std::string> sharedLocks;
    std::set<std::string> exclusiveLocks;
    std::vector<std::pair<std::string, int>> writes;
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
    std::map<std::string, std::vector<Version>> rows;
    int clock = 0;
};

class LockManager
{
public:
    bool acquire(Transaction& tx, const std::string& key, LockMode mode);
    void releaseAll(Transaction& tx);
    void abort(Transaction& tx);
    void printWaitGraph() const;

private:
    struct Lock
    {
        std::set<int> sharedOwners;
        int exclusiveOwner = -1;
    };

    std::map<std::string, Lock> lockTable;
    std::map<int, std::set<int>> waitsFor;

    std::set<int> blockersFor(int txId, const Lock& lock, LockMode mode) const;
    void grant(Transaction& tx, const std::string& key, Lock& lock, LockMode mode);
    bool hasDeadlock() const;
    bool hasCycleFrom(int txId, std::set<int>& seen, std::set<int>& activePath) const;
    void removeFromWaitGraph(int txId);
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
    LockManager locks;
    int nextTxId = 1;
};

#endif
