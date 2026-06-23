#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

enum class TxState
{
    Active,
    Committed,
    Aborted
};

struct Version
{
    int value;
    int writer;
    int commitTime;
};

struct Transaction
{
    int id;
    int startTime;
    TxState state{TxState::Active};
    std::map<std::string, int> pendingWrites;
    std::set<std::string> ownedWriteLocks;
};

struct LockSlot
{
    std::optional<int> writer;
};

class TransactionLab
{
public:
    void seed(const std::string& key, int value)
    {
        data[key].push_back({value, 0, clock});
    }

    int begin()
    {
        const int id = nextTxnId++;
        transactions[id] = Transaction{id, clock};
        std::cout << "T" << id << " begins at ts=" << clock << '\n';
        return id;
    }

    std::optional<int> read(int txnId, const std::string& key)
    {
        Transaction& txn = getActive(txnId);

        auto ownWrite = txn.pendingWrites.find(key);
        if (ownWrite != txn.pendingWrites.end())
        {
            std::cout << "T" << txnId << " reads own write " << key
                      << "=" << ownWrite->second << '\n';
            return ownWrite->second;
        }

        auto tableIt = data.find(key);
        if (tableIt == data.end())
        {
            std::cout << "T" << txnId << " reads " << key << "=NULL\n";
            return std::nullopt;
        }

        const auto& chain = tableIt->second;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            if (it->commitTime <= txn.startTime)
            {
                std::cout << "T" << txnId << " reads " << key
                          << "=" << it->value
                          << " from commit ts=" << it->commitTime << '\n';
                return it->value;
            }
        }

        std::cout << "T" << txnId << " sees no visible version for " << key << '\n';
        return std::nullopt;
    }

    bool write(int txnId, const std::string& key, int value)
    {
        Transaction& txn = getActive(txnId);

        if (!takeWriteLock(txn, key))
        {
            return false;
        }

        txn.pendingWrites[key] = value;
        std::cout << "T" << txnId << " buffers " << key << "=" << value << '\n';
        return true;
    }

    void commit(int txnId)
    {
        Transaction& txn = getActive(txnId);
        const int commitTs = ++clock;

        for (const auto& [key, value] : txn.pendingWrites)
        {
            data[key].push_back({value, txnId, commitTs});
            std::cout << "T" << txnId << " commits version "
                      << key << "=" << value
                      << " at ts=" << commitTs << '\n';
        }

        txn.state = TxState::Committed;
        releaseLocks(txn);
        removeWaits(txnId);
        std::cout << "T" << txnId << " committed\n";
    }

    void abort(int txnId)
    {
        Transaction& txn = transactions.at(txnId);
        if (txn.state != TxState::Active)
        {
            return;
        }

        txn.pendingWrites.clear();
        txn.state = TxState::Aborted;
        releaseLocks(txn);
        removeWaits(txnId);
        std::cout << "T" << txnId << " aborted\n";
    }

    void printVersions(const std::string& key) const
    {
        std::cout << "Version chain for " << key << ": ";
        auto it = data.find(key);
        if (it == data.end())
        {
            std::cout << "empty\n";
            return;
        }

        for (const Version& version : it->second)
        {
            std::cout << "[value=" << version.value
                      << ", writer=T" << version.writer
                      << ", ts=" << version.commitTime << "] ";
        }
        std::cout << '\n';
    }

private:
    int clock{1};
    int nextTxnId{1};
    std::unordered_map<int, Transaction> transactions;
    std::unordered_map<std::string, LockSlot> lockTable;
    std::unordered_map<std::string, std::vector<Version>> data;
    std::unordered_map<int, std::set<int>> waitGraph;

    Transaction& getActive(int txnId)
    {
        Transaction& txn = transactions.at(txnId);
        if (txn.state != TxState::Active)
        {
            throw std::runtime_error("Transaction is not active");
        }
        return txn;
    }

    bool takeWriteLock(Transaction& txn, const std::string& key)
    {
        LockSlot& slot = lockTable[key];

        if (!slot.writer.has_value() || slot.writer.value() == txn.id)
        {
            slot.writer = txn.id;
            txn.ownedWriteLocks.insert(key);
            waitGraph[txn.id].erase(slot.writer.value_or(txn.id));
            std::cout << "T" << txn.id << " gets X-lock on " << key << '\n';
            return true;
        }

        const int blocker = slot.writer.value();
        waitGraph[txn.id].insert(blocker);
        std::cout << "T" << txn.id << " waits for T" << blocker
                  << " on " << key << '\n';

        if (hasCycle(txn.id, txn.id, {}))
        {
            std::cout << "Deadlock detected involving T" << txn.id << '\n';
            abort(txn.id);
        }

        return false;
    }

    bool hasCycle(int start, int current, std::set<int> visited) const
    {
        if (!visited.insert(current).second)
        {
            return false;
        }

        auto it = waitGraph.find(current);
        if (it == waitGraph.end())
        {
            return false;
        }

        for (int next : it->second)
        {
            if (next == start)
            {
                return true;
            }
            if (hasCycle(start, next, visited))
            {
                return true;
            }
        }

        return false;
    }

    void releaseLocks(Transaction& txn)
    {
        for (const std::string& key : txn.ownedWriteLocks)
        {
            auto lockIt = lockTable.find(key);
            if (lockIt != lockTable.end() && lockIt->second.writer == txn.id)
            {
                lockIt->second.writer.reset();
                std::cout << "T" << txn.id << " releases X-lock on " << key << '\n';
            }
        }

        txn.ownedWriteLocks.clear();
    }

    void removeWaits(int txnId)
    {
        waitGraph.erase(txnId);
        for (auto& [_, blockers] : waitGraph)
        {
            blockers.erase(txnId);
        }
    }
};

void snapshotDemo()
{
    std::cout << "\n--- MVCC snapshot demo ---\n";
    TransactionLab lab;
    lab.seed("account_A", 100);

    const int t1 = lab.begin();
    lab.read(t1, "account_A");

    const int t2 = lab.begin();
    lab.write(t2, "account_A", 130);
    lab.commit(t2);

    lab.read(t1, "account_A");
    lab.commit(t1);

    const int t3 = lab.begin();
    lab.read(t3, "account_A");
    lab.commit(t3);

    lab.printVersions("account_A");
}

void deadlockDemo()
{
    std::cout << "\n--- Strict 2PL deadlock demo ---\n";
    TransactionLab lab;
    lab.seed("row_X", 10);
    lab.seed("row_Y", 20);

    const int t1 = lab.begin();
    const int t2 = lab.begin();

    lab.write(t1, "row_X", 11);
    lab.write(t2, "row_Y", 21);

    lab.write(t1, "row_Y", 12);
    lab.write(t2, "row_X", 22);

    lab.commit(t1);
    lab.printVersions("row_X");
    lab.printVersions("row_Y");
}

int main()
{
    try
    {
        snapshotDemo();
        deadlockDemo();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
