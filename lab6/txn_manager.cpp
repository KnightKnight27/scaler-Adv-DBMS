#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>
#include <queue>
#include <memory>

using namespace std;

using TxnId = int;
using Timestamp = int;
using Key = int;

struct Version {
    int value;
    Timestamp begin_ts;
    Timestamp end_ts;

    Version(int v, Timestamp b)
        : value(v), begin_ts(b), end_ts(INT32_MAX) {}
};

struct Transaction {
    TxnId id = 0;
    Timestamp start_ts = 0;

    bool active = true;
    bool aborted = false;

    unordered_set<Key> locked_keys;

    Transaction() = default;

    Transaction(TxnId id, Timestamp ts)
        : id(id), start_ts(ts) {}
};

class LockManager {
private:
    unordered_map<Key, TxnId> owners;

public:
    bool tryLock(TxnId txn, Key key) {
        auto it = owners.find(key);

        if (it == owners.end()) {
            owners[key] = txn;
            return true;
        }

        return it->second == txn;
    }

    TxnId owner(Key key) {
        auto it = owners.find(key);

        if (it == owners.end())
            return -1;

        return it->second;
    }

    void unlock(Key key) {
        owners.erase(key);
    }
};

class TransactionManager {
private:
    Timestamp global_ts = 1;
    TxnId next_txn = 1;

    unordered_map<TxnId, Transaction> transactions;
    unordered_map<Key, vector<Version>> table;

    LockManager lockManager;

    // waits-for graph
    unordered_map<TxnId, unordered_set<TxnId>> waits;

    bool dfs(TxnId node,
             unordered_set<TxnId>& visited,
             unordered_set<TxnId>& stack,
             vector<TxnId>& cycle)
    {
        visited.insert(node);
        stack.insert(node);

        for (TxnId nxt : waits[node]) {
            if (!visited.count(nxt)) {
                if (dfs(nxt, visited, stack, cycle))
                    return true;
            }
            else if (stack.count(nxt)) {
                cycle.push_back(nxt);
                return true;
            }
        }

        stack.erase(node);
        return false;
    }

    bool detectCycle(vector<TxnId>& cycle) {
        unordered_set<TxnId> visited;
        unordered_set<TxnId> stack;

        for (auto& p : waits) {
            TxnId node = p.first;

            if (!visited.count(node)) {
                if (dfs(node, visited, stack, cycle))
                    return true;
            }
        }

        return false;
    }

    void resolveDeadlock() {
        vector<TxnId> cycle;

        if (!detectCycle(cycle))
            return;

        TxnId victim = -1;

        for (auto tx : cycle)
            victim = max(victim, tx);

        cout << "Deadlock detected. Aborting T"
             << victim << "\n";

        abort(victim);
    }

public:
    TxnId begin() {
        TxnId id = next_txn++;

        transactions.emplace(
            id,
            Transaction(id, global_ts++)
        );

        cout << "Begin T" << id
             << " snapshot=" << transactions[id].start_ts
             << "\n";

        return id;
    }

    int read(TxnId txn, Key key) {
        Transaction& t = transactions.at(txn);

        if (!table.count(key))
            throw runtime_error("Missing key");

        auto& versions = table[key];

        for (auto it = versions.rbegin();
             it != versions.rend();
             ++it)
        {
            if (it->begin_ts <= t.start_ts &&
                t.start_ts < it->end_ts)
            {
                return it->value;
            }
        }

        throw runtime_error("No visible version");
    }

    bool write(TxnId txn, Key key, int value) {
        Transaction& t = transactions.at(txn);

        if (!t.active)
            return false;

        if (!lockManager.tryLock(txn, key)) {
            TxnId owner = lockManager.owner(key);

            waits[txn].insert(owner);

            cout << "T" << txn
                 << " waits for T"
                 << owner << "\n";

            resolveDeadlock();

            return false;
        }

        t.locked_keys.insert(key);

        auto& versions = table[key];

        if (!versions.empty())
            versions.back().end_ts = global_ts;

        versions.emplace_back(value, global_ts++);

        cout << "T" << txn
             << " wrote "
             << value
             << " to key "
             << key
             << "\n";

        return true;
    }

    void commit(TxnId txn) {
        Transaction& t = transactions.at(txn);

        if (t.aborted)
            return;

        for (Key k : t.locked_keys)
            lockManager.unlock(k);

        t.locked_keys.clear();
        t.active = false;

        waits.erase(txn);

        for (auto& p : waits)
            p.second.erase(txn);

        cout << "Commit T" << txn << "\n";
    }

    void abort(TxnId txn) {
        Transaction& t = transactions.at(txn);

        t.aborted = true;
        t.active = false;

        for (Key k : t.locked_keys)
            lockManager.unlock(k);

        t.locked_keys.clear();

        waits.erase(txn);

        for (auto& p : waits)
            p.second.erase(txn);

        cout << "Abort T" << txn << "\n";
    }

    void insert(Key key, int value) {
        table[key].emplace_back(value, 0);
    }

    void print(Key key) {
        cout << "Key " << key << " versions:\n";

        for (auto& v : table[key]) {
            cout << "  value=" << v.value
                 << " begin=" << v.begin_ts
                 << " end=" << v.end_ts
                 << "\n";
        }
    }
};

int main() {
    TransactionManager tm;

    tm.insert(1, 100);
    tm.insert(2, 200);

    auto t1 = tm.begin();
    auto t2 = tm.begin();

    cout << "T1 reads key1 = "
         << tm.read(t1, 1)
         << "\n";

    cout << "T2 reads key1 = "
         << tm.read(t2, 1)
         << "\n";

    tm.write(t1, 1, 150);

    // T2 attempts same row
    tm.write(t2, 1, 175);

    tm.commit(t1);

    auto t3 = tm.begin();

    cout << "T3 reads key1 = "
         << tm.read(t3, 1)
         << "\n";

    // Deadlock example
    auto a = tm.begin();
    auto b = tm.begin();

    tm.write(a, 1, 300);
    tm.write(b, 2, 400);

    tm.write(a, 2, 500);
    tm.write(b, 1, 600);

    tm.print(1);
    tm.print(2);

    return 0;
}