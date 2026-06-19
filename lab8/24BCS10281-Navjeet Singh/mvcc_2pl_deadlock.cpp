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

// =====================================================
// Basic Types
// =====================================================

using TransactionId = uint64_t;
using RecordKey = std::string;

// =====================================================
// Transaction Metadata
// =====================================================

enum class TxState {
    RUNNING,
    COMMITTED,
    ABORTED
};

struct TxContext {
    TransactionId txid;
    TransactionId snapshot;
    TxState state = TxState::RUNNING;
    bool shrinkingPhase = false;
};

static std::atomic<TransactionId> globalTxCounter{1};

static std::mutex txTableMutex;
static std::unordered_map<TransactionId, TxContext> txTable;

// =====================================================
// Transaction Lifecycle
// =====================================================

TransactionId startTx() {
    std::lock_guard guard(txTableMutex);

    TransactionId id = globalTxCounter.fetch_add(1);

    txTable[id] = {
        id,
        id,
        TxState::RUNNING,
        false
    };

    return id;
}

bool transactionCommitted(TransactionId id) {
    std::lock_guard guard(txTableMutex);

    return txTable.count(id) &&
           txTable[id].state == TxState::COMMITTED;
}

bool transactionAborted(TransactionId id) {
    std::lock_guard guard(txTableMutex);

    return txTable.count(id) &&
           txTable[id].state == TxState::ABORTED;
}

// =====================================================
// MVCC Storage
// =====================================================

struct VersionNode {
    std::string payload;
    TransactionId creator;
    TransactionId remover;
};

static std::mutex storageMutex;

static std::unordered_map<
    RecordKey,
    std::list<VersionNode>
> versionStore;

bool visibleToReader(
    const VersionNode& version,
    TransactionId snapshot,
    TransactionId reader) {

    bool creatorVisible =
        (version.creator == reader) ||
        (transactionCommitted(version.creator) &&
         version.creator < snapshot);

    if (!creatorVisible)
        return false;

    if (version.remover == 0)
        return true;

    bool deletedVisible =
        (version.remover == reader) ||
        (transactionCommitted(version.remover) &&
         version.remover < snapshot);

    return !deletedVisible;
}

std::optional<std::string> fetchRecord(
    const RecordKey& key,
    TransactionId reader) {

    std::lock_guard guard(storageMutex);

    TransactionId snapshot;

    {
        std::lock_guard txGuard(txTableMutex);
        snapshot = txTable[reader].snapshot;
    }

    if (!versionStore.count(key))
        return std::nullopt;

    for (const auto& version : versionStore[key]) {
        if (visibleToReader(version, snapshot, reader))
            return version.payload;
    }

    return std::nullopt;
}

void createRecord(
    const RecordKey& key,
    const std::string& value,
    TransactionId txid) {

    std::lock_guard guard(storageMutex);

    versionStore[key].push_front({
        value,
        txid,
        0
    });
}

void modifyRecord(
    const RecordKey& key,
    const std::string& value,
    TransactionId txid) {

    std::lock_guard guard(storageMutex);

    TransactionId snapshot;

    {
        std::lock_guard txGuard(txTableMutex);
        snapshot = txTable[txid].snapshot;
    }

    if (versionStore.count(key)) {
        for (auto& version : versionStore[key]) {
            if (visibleToReader(version, snapshot, txid) &&
                version.remover == 0) {

                version.remover = txid;
                break;
            }
        }
    }

    versionStore[key].push_front({
        value,
        txid,
        0
    });
}

void eraseRecord(
    const RecordKey& key,
    TransactionId txid) {

    std::lock_guard guard(storageMutex);

    TransactionId snapshot;

    {
        std::lock_guard txGuard(txTableMutex);
        snapshot = txTable[txid].snapshot;
    }

    if (!versionStore.count(key))
        return;

    for (auto& version : versionStore[key]) {
        if (visibleToReader(version, snapshot, txid) &&
            version.remover == 0) {

            version.remover = txid;
            return;
        }
    }
}

// =====================================================
// Locking Layer
// =====================================================

enum class LockType {
    READ,
    WRITE
};

struct LockEntry {
    TransactionId owner;
    LockType type;
    bool approved = false;
};

struct ResourceLock {
    std::list<LockEntry> queue;
    std::mutex guard;
    std::condition_variable cv;
};

static std::unordered_map<RecordKey, ResourceLock> lockDirectory;

static std::mutex waitGraphMutex;

static std::unordered_map<
    TransactionId,
    std::unordered_set<TransactionId>
> waitGraph;

class DeadlockDetected : public std::runtime_error {
public:
    explicit DeadlockDetected(TransactionId txid)
        : std::runtime_error(
              "Deadlock detected for TX " +
              std::to_string(txid)) {}
};

// =====================================================
// Wait-For Graph
// =====================================================

bool detectCycleDFS(
    TransactionId node,
    std::unordered_map<TransactionId,
    std::unordered_set<TransactionId>>& graph,
    std::unordered_set<TransactionId>& visited,
    std::unordered_set<TransactionId>& recursion) {

    visited.insert(node);
    recursion.insert(node);

    for (auto neighbour : graph[node]) {

        if (!visited.count(neighbour)) {
            if (detectCycleDFS(
                    neighbour,
                    graph,
                    visited,
                    recursion))
                return true;
        }

        if (recursion.count(neighbour))
            return true;
    }

    recursion.erase(node);
    return false;
}

bool cycleExists(
    TransactionId start,
    std::unordered_map<TransactionId,
    std::unordered_set<TransactionId>>& graph) {

    std::unordered_set<TransactionId> visited;
    std::unordered_set<TransactionId> recursion;

    return detectCycleDFS(
        start,
        graph,
        visited,
        recursion);
}

// =====================================================
// Lock Acquisition
// =====================================================

void lockRecord(
    const RecordKey& key,
    TransactionId txid,
    LockType requested) {

    auto& bucket = lockDirectory[key];

    std::unique_lock lock(bucket.guard);

    bucket.queue.push_back({
        txid,
        requested,
        false
    });

    auto current =
        std::prev(bucket.queue.end());

    while (true) {

        bool blocked = false;
        std::unordered_set<TransactionId> blockers;

        for (auto& req : bucket.queue) {

            if (&req == &(*current))
                break;

            if (!req.approved)
                continue;

            if (requested == LockType::WRITE ||
                req.type == LockType::WRITE) {

                blocked = true;
                blockers.insert(req.owner);
            }
        }

        if (!blocked) {

            current->approved = true;

            std::lock_guard graphLock(waitGraphMutex);
            waitGraph.erase(txid);

            return;
        }

        {
            std::lock_guard graphLock(waitGraphMutex);

            waitGraph[txid] = blockers;

            if (cycleExists(txid, waitGraph)) {
                bucket.queue.erase(current);
                throw DeadlockDetected(txid);
            }
        }

        bucket.cv.wait(lock);
    }
}

void unlockAll(TransactionId txid) {

    {
        std::lock_guard txGuard(txTableMutex);
        txTable[txid].shrinkingPhase = true;
    }

    for (auto& [key, resource] : lockDirectory) {

        std::lock_guard lock(resource.guard);

        resource.queue.remove_if(
            [&](const LockEntry& entry) {
                return entry.owner == txid;
            });

        resource.cv.notify_all();
    }

    std::lock_guard graphLock(waitGraphMutex);
    waitGraph.erase(txid);
}

// =====================================================
// Database Engine
// =====================================================

class DatabaseEngine {
public:

    TransactionId begin() {
        return startTx();
    }

    std::optional<std::string> read(
        TransactionId txid,
        const RecordKey& key) {

        lockRecord(key, txid, LockType::READ);
        return fetchRecord(key, txid);
    }

    void insert(
        TransactionId txid,
        const RecordKey& key,
        const std::string& value) {

        lockRecord(key, txid, LockType::WRITE);
        createRecord(key, value, txid);
    }

    void update(
        TransactionId txid,
        const RecordKey& key,
        const std::string& value) {

        lockRecord(key, txid, LockType::WRITE);
        modifyRecord(key, value, txid);
    }

    void remove(
        TransactionId txid,
        const RecordKey& key) {

        lockRecord(key, txid, LockType::WRITE);
        eraseRecord(key, txid);
    }

    void commit(TransactionId txid) {

        {
            std::lock_guard txGuard(txTableMutex);
            txTable[txid].state = TxState::COMMITTED;
        }

        unlockAll(txid);

        std::cout
            << "[TX "
            << txid
            << "] COMMITTED\n";
    }

    void rollback(TransactionId txid) {

        {
            std::lock_guard storageGuard(storageMutex);

            for (auto& [key, chain] : versionStore) {

                for (auto& version : chain) {

                    if (version.creator == txid)
                        version.remover = txid;

                    if (version.remover == txid)
                        version.remover = 0;
                }
            }
        }

        {
            std::lock_guard txGuard(txTableMutex);
            txTable[txid].state = TxState::ABORTED;
        }

        unlockAll(txid);

        std::cout
            << "[TX "
            << txid
            << "] ABORTED\n";
    }
};

// =====================================================
// Utility
// =====================================================

void showValue(
    const std::optional<std::string>& value,
    TransactionId txid,
    const RecordKey& key) {

    std::cout
        << "[TX "
        << txid
        << "] "
        << key
        << " = "
        << (value ? *value : "<not visible>")
        << '\n';
}

// =====================================================
// Demonstration
// =====================================================

int main() {

    DatabaseEngine db;

    std::cout << "=== MVCC TEST ===\n";

    auto tx1 = db.begin();

    db.insert(tx1, "balance", "1000");
    db.commit(tx1);

    auto tx2 = db.begin();
    auto tx3 = db.begin();

    db.update(tx3, "balance", "2000");
    db.commit(tx3);

    showValue(
        db.read(tx2, "balance"),
        tx2,
        "balance");

    db.commit(tx2);

    std::cout << "\n=== LOCK TEST ===\n";

    auto tx4 = db.begin();
    auto tx5 = db.begin();

    showValue(
        db.read(tx4, "balance"),
        tx4,
        "balance");

    showValue(
        db.read(tx5, "balance"),
        tx5,
        "balance");

    db.commit(tx4);
    db.commit(tx5);

    std::cout << "\n=== DONE ===\n";

    return 0;
}