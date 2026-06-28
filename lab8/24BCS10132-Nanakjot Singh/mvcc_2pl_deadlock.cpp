#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include <string>
#include <stdexcept>

using TxnID = uint64_t;
using ItemKey = std::string;

enum class TxnStatus {
    ACTIVE,
    DONE,
    ROLLED_BACK
};

struct TxnInfo {
    TxnID id;
    TxnID snapID;
    TxnStatus status = TxnStatus::ACTIVE;
    bool releasingLocks = false;
};

static std::atomic<TxnID> nextTxnID{1};

static std::mutex txnRegistryMtx;
static std::unordered_map<TxnID, TxnInfo> txnRegistry;

TxnID openTxn() {
    std::lock_guard lk(txnRegistryMtx);

    TxnID id = nextTxnID.fetch_add(1);

    txnRegistry[id] = {
        id,
        id,
        TxnStatus::ACTIVE,
        false
    };

    return id;
}

bool isTxnDone(TxnID id) {
    std::lock_guard lk(txnRegistryMtx);

    return txnRegistry.count(id) &&
           txnRegistry[id].status == TxnStatus::DONE;
}

bool isTxnRolledBack(TxnID id) {
    std::lock_guard lk(txnRegistryMtx);

    return txnRegistry.count(id) &&
           txnRegistry[id].status == TxnStatus::ROLLED_BACK;
}

struct RowVersion {
    std::string data;
    TxnID writtenBy;
    TxnID deletedBy;
};

static std::mutex storeMtx;

static std::unordered_map<
    ItemKey,
    std::list<RowVersion>
> dataStore;

bool canSeeVersion(
    const RowVersion& ver,
    TxnID snapID,
    TxnID readerID) {

    bool writerSeen =
        (ver.writtenBy == readerID) ||
        (isTxnDone(ver.writtenBy) &&
         ver.writtenBy < snapID);

    if (!writerSeen)
        return false;

    if (ver.deletedBy == 0)
        return true;

    bool deleterSeen =
        (ver.deletedBy == readerID) ||
        (isTxnDone(ver.deletedBy) &&
         ver.deletedBy < snapID);

    return !deleterSeen;
}

std::optional<std::string> getItem(
    const ItemKey& key,
    TxnID readerID) {

    std::lock_guard lk(storeMtx);

    TxnID snapID;

    {
        std::lock_guard txLk(txnRegistryMtx);
        snapID = txnRegistry[readerID].snapID;
    }

    if (!dataStore.count(key))
        return std::nullopt;

    for (const auto& ver : dataStore[key]) {
        if (canSeeVersion(ver, snapID, readerID))
            return ver.data;
    }

    return std::nullopt;
}

void addItem(
    const ItemKey& key,
    const std::string& val,
    TxnID txid) {

    std::lock_guard lk(storeMtx);

    dataStore[key].push_front({
        val,
        txid,
        0
    });
}

void changeItem(
    const ItemKey& key,
    const std::string& val,
    TxnID txid) {

    std::lock_guard lk(storeMtx);

    TxnID snapID;

    {
        std::lock_guard txLk(txnRegistryMtx);
        snapID = txnRegistry[txid].snapID;
    }

    if (dataStore.count(key)) {
        for (auto& ver : dataStore[key]) {
            if (canSeeVersion(ver, snapID, txid) &&
                ver.deletedBy == 0) {

                ver.deletedBy = txid;
                break;
            }
        }
    }

    dataStore[key].push_front({
        val,
        txid,
        0
    });
}

void removeItem(
    const ItemKey& key,
    TxnID txid) {

    std::lock_guard lk(storeMtx);

    TxnID snapID;

    {
        std::lock_guard txLk(txnRegistryMtx);
        snapID = txnRegistry[txid].snapID;
    }

    if (!dataStore.count(key))
        return;

    for (auto& ver : dataStore[key]) {
        if (canSeeVersion(ver, snapID, txid) &&
            ver.deletedBy == 0) {

            ver.deletedBy = txid;
            return;
        }
    }
}

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

struct LockSlot {
    TxnID holder;
    LockMode mode;
    bool granted = false;
};

struct KeyLock {
    std::list<LockSlot> waitQueue;
    std::mutex mtx;
    std::condition_variable signal;
};

static std::unordered_map<ItemKey, KeyLock> lockTable;

static std::mutex depGraphMtx;

static std::unordered_map<
    TxnID,
    std::unordered_set<TxnID>
> depGraph;

class DeadlockError : public std::runtime_error {
public:
    explicit DeadlockError(TxnID txid)
        : std::runtime_error(
              "Deadlock found for TXN " +
              std::to_string(txid)) {}
};

bool hasCycleDFS(
    TxnID node,
    std::unordered_map<TxnID,
    std::unordered_set<TxnID>>& graph,
    std::unordered_set<TxnID>& seen,
    std::unordered_set<TxnID>& path) {

    seen.insert(node);
    path.insert(node);

    for (auto adj : graph[node]) {

        if (!seen.count(adj)) {
            if (hasCycleDFS(
                    adj,
                    graph,
                    seen,
                    path))
                return true;
        }

        if (path.count(adj))
            return true;
    }

    path.erase(node);
    return false;
}

bool foundCycle(
    TxnID start,
    std::unordered_map<TxnID,
    std::unordered_set<TxnID>>& graph) {

    std::unordered_set<TxnID> seen;
    std::unordered_set<TxnID> path;

    return hasCycleDFS(
        start,
        graph,
        seen,
        path);
}

void acquireLock(
    const ItemKey& key,
    TxnID txid,
    LockMode desired) {

    auto& slot = lockTable[key];

    std::unique_lock ul(slot.mtx);

    slot.waitQueue.push_back({
        txid,
        desired,
        false
    });

    auto self =
        std::prev(slot.waitQueue.end());

    while (true) {

        bool waiting = false;
        std::unordered_set<TxnID> blockedBy;

        for (auto& entry : slot.waitQueue) {

            if (&entry == &(*self))
                break;

            if (!entry.granted)
                continue;

            if (desired == LockMode::EXCLUSIVE ||
                entry.mode == LockMode::EXCLUSIVE) {

                waiting = true;
                blockedBy.insert(entry.holder);
            }
        }

        if (!waiting) {

            self->granted = true;

            std::lock_guard gl(depGraphMtx);
            depGraph.erase(txid);

            return;
        }

        {
            std::lock_guard gl(depGraphMtx);

            depGraph[txid] = blockedBy;

            if (foundCycle(txid, depGraph)) {
                slot.waitQueue.erase(self);
                throw DeadlockError(txid);
            }
        }

        slot.signal.wait(ul);
    }
}

void releaseAll(TxnID txid) {

    {
        std::lock_guard txLk(txnRegistryMtx);
        txnRegistry[txid].releasingLocks = true;
    }

    for (auto& [key, kl] : lockTable) {

        std::lock_guard lk(kl.mtx);

        kl.waitQueue.remove_if(
            [&](const LockSlot& s) {
                return s.holder == txid;
            });

        kl.signal.notify_all();
    }

    std::lock_guard gl(depGraphMtx);
    depGraph.erase(txid);
}

class StorageEngine {
public:

    TxnID beginTxn() {
        return openTxn();
    }

    std::optional<std::string> get(
        TxnID txid,
        const ItemKey& key) {

        acquireLock(key, txid, LockMode::SHARED);
        return getItem(key, txid);
    }

    void put(
        TxnID txid,
        const ItemKey& key,
        const std::string& val) {

        acquireLock(key, txid, LockMode::EXCLUSIVE);
        addItem(key, val, txid);
    }

    void set(
        TxnID txid,
        const ItemKey& key,
        const std::string& val) {

        acquireLock(key, txid, LockMode::EXCLUSIVE);
        changeItem(key, val, txid);
    }

    void del(
        TxnID txid,
        const ItemKey& key) {

        acquireLock(key, txid, LockMode::EXCLUSIVE);
        removeItem(key, txid);
    }

    void commitTxn(TxnID txid) {

        {
            std::lock_guard txLk(txnRegistryMtx);
            txnRegistry[txid].status = TxnStatus::DONE;
        }

        releaseAll(txid);

        std::cout
            << "[TXN "
            << txid
            << "] COMMITTED\n";
    }

    void abortTxn(TxnID txid) {

        {
            std::lock_guard sLk(storeMtx);

            for (auto& [key, chain] : dataStore) {

                for (auto& ver : chain) {

                    if (ver.writtenBy == txid)
                        ver.deletedBy = txid;

                    if (ver.deletedBy == txid)
                        ver.deletedBy = 0;
                }
            }
        }

        {
            std::lock_guard txLk(txnRegistryMtx);
            txnRegistry[txid].status = TxnStatus::ROLLED_BACK;
        }

        releaseAll(txid);

        std::cout
            << "[TXN "
            << txid
            << "] ABORTED\n";
    }
};

void printResult(
    const std::optional<std::string>& val,
    TxnID txid,
    const ItemKey& key) {

    std::cout
        << "[TXN "
        << txid
        << "] "
        << key
        << " = "
        << (val ? *val : "<invisible>")
        << '\n';
}

int main() {

    StorageEngine engine;

    std::cout << "=== MVCC TEST ===\n";

    auto t1 = engine.beginTxn();

    engine.put(t1, "balance", "1000");
    engine.commitTxn(t1);

    auto t2 = engine.beginTxn();
    auto t3 = engine.beginTxn();

    engine.set(t3, "balance", "2000");
    engine.commitTxn(t3);

    printResult(
        engine.get(t2, "balance"),
        t2,
        "balance");

    engine.commitTxn(t2);

    std::cout << "\n=== LOCK TEST ===\n";

    auto t4 = engine.beginTxn();
    auto t5 = engine.beginTxn();

    printResult(
        engine.get(t4, "balance"),
        t4,
        "balance");

    printResult(
        engine.get(t5, "balance"),
        t5,
        "balance");

    engine.commitTxn(t4);
    engine.commitTxn(t5);

    std::cout << "\n=== DONE ===\n";

    return 0;
}
