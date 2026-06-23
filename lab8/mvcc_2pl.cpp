#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <string>

using namespace std;

// ======================================================
// Transaction Definitions
// ======================================================

enum class TxState
{
    ACTIVE,
    WAITING,
    COMMITTED,
    ABORTED
};

enum class LockType
{
    SHARED,
    EXCLUSIVE
};

struct Transaction
{
    int id;
    int snapshotTs;
    TxState state = TxState::ACTIVE;

    set<string> sharedLocks;
    set<string> exclusiveLocks;

    vector<pair<string,int>> pendingWrites;
};

// ======================================================
// MVCC
// ======================================================

struct Version
{
    int value;
    int creatorTx;
    int commitTs;
};

class MVCCStore
{
private:

    unordered_map<string, vector<Version>> data;
    int globalClock = 0;

public:

    void initialize(const string& key, int value)
    {
        data[key].push_back({value,0,++globalClock});
    }

    int getSnapshotTimestamp() const
    {
        return globalClock;
    }

    int read(const string& key,
             const Transaction& tx) const
    {
        auto it = data.find(key);

        if(it == data.end())
            throw runtime_error("Key not found");

        const auto& versions = it->second;

        for(auto rit = versions.rbegin();
            rit != versions.rend();
            ++rit)
        {
            if(rit->commitTs <= tx.snapshotTs)
                return rit->value;
        }

        throw runtime_error("No visible version");
    }

    void commit(Transaction& tx)
    {
        int commitTime = ++globalClock;

        for(const auto& write : tx.pendingWrites)
        {
            data[write.first].push_back(
            {
                write.second,
                tx.id,
                commitTime
            });
        }

        tx.state = TxState::COMMITTED;
    }

    void printVersions() const
    {
        cout << "\n====== MVCC VERSION CHAINS ======\n";

        for(const auto& row : data)
        {
            cout << row.first << " : ";

            for(const auto& version : row.second)
            {
                cout
                    << "[value=" << version.value
                    << ", tx=" << version.creatorTx
                    << ", ts=" << version.commitTs
                    << "] ";
            }

            cout << '\n';
        }
    }
};

// ======================================================
// Lock Manager (Strict 2PL)
// ======================================================

struct LockEntry
{
    set<int> sharedOwners;
    int exclusiveOwner = -1;
};

class LockManager
{
private:

    unordered_map<string, LockEntry> lockTable;

    unordered_map<int,set<int>> waitGraph;

private:

    bool detectCycleDFS(
        int node,
        set<int>& visited,
        set<int>& recursionStack) const
    {
        recursionStack.insert(node);
        visited.insert(node);

        auto it = waitGraph.find(node);

        if(it != waitGraph.end())
        {
            for(int next : it->second)
            {
                if(recursionStack.count(next))
                    return true;

                if(!visited.count(next))
                {
                    if(detectCycleDFS(
                        next,
                        visited,
                        recursionStack))
                    {
                        return true;
                    }
                }
            }
        }

        recursionStack.erase(node);
        return false;
    }

    bool hasDeadlock() const
    {
        set<int> visited;

        for(const auto& node : waitGraph)
        {
            set<int> path;

            if(detectCycleDFS(
                node.first,
                visited,
                path))
            {
                return true;
            }
        }

        return false;
    }

public:

    bool acquire(
        Transaction& tx,
        const string& key,
        LockType type)
    {
        LockEntry& lock = lockTable[key];

        set<int> blockers;

        if(lock.exclusiveOwner != -1 &&
           lock.exclusiveOwner != tx.id)
        {
            blockers.insert(
                lock.exclusiveOwner);
        }

        if(type == LockType::EXCLUSIVE)
        {
            for(int owner :
                lock.sharedOwners)
            {
                if(owner != tx.id)
                    blockers.insert(owner);
            }
        }

        if(blockers.empty())
        {
            waitGraph.erase(tx.id);

            if(type == LockType::SHARED)
            {
                lock.sharedOwners.insert(tx.id);
                tx.sharedLocks.insert(key);

                cout
                    << "[LOCK] T"
                    << tx.id
                    << " acquired S-lock on "
                    << key
                    << '\n';
            }
            else
            {
                lock.sharedOwners.erase(tx.id);

                tx.sharedLocks.erase(key);

                lock.exclusiveOwner = tx.id;

                tx.exclusiveLocks.insert(key);

                cout
                    << "[LOCK] T"
                    << tx.id
                    << " acquired X-lock on "
                    << key
                    << '\n';
            }

            return true;
        }

        waitGraph[tx.id] = blockers;

        tx.state = TxState::WAITING;

        cout
            << "[WAIT] T"
            << tx.id
            << " waiting for ";

        for(int b : blockers)
            cout << "T" << b << " ";

        cout << '\n';

        if(hasDeadlock())
        {
            cout
                << "[DEADLOCK] detected\n";

            return false;
        }

        return false;
    }

    void releaseAll(Transaction& tx)
    {
        for(const auto& key :
            tx.sharedLocks)
        {
            lockTable[key]
                .sharedOwners
                .erase(tx.id);
        }

        for(const auto& key :
            tx.exclusiveLocks)
        {
            if(lockTable[key]
                    .exclusiveOwner
                == tx.id)
            {
                lockTable[key]
                    .exclusiveOwner = -1;
            }
        }

        tx.sharedLocks.clear();
        tx.exclusiveLocks.clear();

        waitGraph.erase(tx.id);

        for(auto& edge : waitGraph)
        {
            edge.second.erase(tx.id);
        }
    }

    void showLocks() const
    {
        cout
            << "\n====== LOCK TABLE ======\n";

        for(const auto& entry :
            lockTable)
        {
            cout
                << entry.first
                << " -> ";

            if(entry.second.exclusiveOwner
                != -1)
            {
                cout
                    << "X(T"
                    << entry.second
                           .exclusiveOwner
                    << ")";
            }

            for(int tx :
                entry.second.sharedOwners)
            {
                cout
                    << "S(T"
                    << tx
                    << ") ";
            }

            cout << '\n';
        }
    }

    void showWaitGraph() const
    {
        cout
            << "\n====== WAIT FOR GRAPH ======\n";

        if(waitGraph.empty())
        {
            cout << "empty\n";
            return;
        }

        for(const auto& edge :
            waitGraph)
        {
            cout
                << "T"
                << edge.first
                << " -> ";

            for(int tx :
                edge.second)
            {
                cout
                    << "T"
                    << tx
                    << " ";
            }

            cout << '\n';
        }
    }
};

// ======================================================
// Transaction Manager
// ======================================================

class TransactionManager
{
private:

    MVCCStore store;
    LockManager locks;

    int nextTxId = 1;

public:

    TransactionManager()
    {
        store.initialize("A",100);
        store.initialize("B",200);
    }

    Transaction begin()
    {
        Transaction tx;

        tx.id = nextTxId++;

        tx.snapshotTs =
            store.getSnapshotTimestamp();

        cout
            << "\n[BEGIN] T"
            << tx.id
            << " snapshot="
            << tx.snapshotTs
            << '\n';

        return tx;
    }

    void read(
        Transaction& tx,
        const string& key)
    {
        if(tx.state ==
           TxState::ABORTED)
            return;

        if(locks.acquire(
            tx,
            key,
            LockType::SHARED))
        {
            cout
                << "[READ] T"
                << tx.id
                << " "
                << key
                << "="
                << store.read(
                       key,
                       tx)
                << '\n';
        }
    }

    void write(
        Transaction& tx,
        const string& key,
        int value)
    {
        if(tx.state ==
           TxState::ABORTED)
            return;

        if(locks.acquire(
            tx,
            key,
            LockType::EXCLUSIVE))
        {
            tx.pendingWrites.push_back(
            {
                key,
                value
            });

            cout
                << "[WRITE] T"
                << tx.id
                << " "
                << key
                << "="
                << value
                << '\n';
        }
    }

    void abort(Transaction& tx)
    {
        tx.pendingWrites.clear();

        tx.state =
            TxState::ABORTED;

        locks.releaseAll(tx);

        cout
            << "[ABORT] T"
            << tx.id
            << '\n';
    }

    void commit(Transaction& tx)
    {
        if(tx.state ==
           TxState::ABORTED)
        {
            cout
                << "T"
                << tx.id
                << " cannot commit\n";

            return;
        }

        store.commit(tx);

        locks.releaseAll(tx);

        cout
            << "[COMMIT] T"
            << tx.id
            << '\n';
    }

    void showSystem() const
    {
        locks.showLocks();
        locks.showWaitGraph();
        store.printVersions();
    }
};

// ======================================================
// Demo
// ======================================================

int main()
{
    TransactionManager tm;

    Transaction t1 = tm.begin();
    Transaction t2 = tm.begin();

    tm.read(t1,"A");
    tm.write(t1,"A",110);

    tm.read(t2,"B");
    tm.write(t2,"B",190);

    cout
        << "\n===== DEADLOCK SCENARIO =====\n";

    tm.write(t1,"B",210);
    tm.write(t2,"A",90);

    tm.showSystem();

    cout
        << "\n===== RESOLUTION =====\n";

    tm.abort(t2);

    tm.write(t1,"B",210);

    tm.commit(t1);

    Transaction t3 = tm.begin();

    tm.read(t3,"A");
    tm.read(t3,"B");

    tm.commit(t3);

    tm.showSystem();

    return 0;
}