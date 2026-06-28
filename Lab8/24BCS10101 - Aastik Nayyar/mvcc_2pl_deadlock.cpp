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

using TransID  = uint64_t;
using RecordKey = std::string;

enum class TransState {
    RUNNING,
    COMMITTED,
    ABORTED
};

struct TransInfo {
    TransID id;
    TransID snapshotAt;
    TransState state = TransState::RUNNING;
    bool unlocking = false;
};

static std::atomic<TransID> txCounter{1};

static std::mutex registryMtx;
static std::unordered_map<TransID, TransInfo> transMap;

TransID startTxn() {
    std::lock_guard lk(registryMtx);

    TransID id = txCounter.fetch_add(1);

    transMap[id] = {
        id,
        id,
        TransState::RUNNING,
        false
    };

    return id;
}

bool isCommitted(TransID id) {
    std::lock_guard lk(registryMtx);

    return transMap.count(id) &&
           transMap[id].state == TransState::COMMITTED;
}

bool isAborted(TransID id) {
    std::lock_guard lk(registryMtx);

    return transMap.count(id) &&
           transMap[id].state == TransState::ABORTED;
}

struct DataVersion {
    std::string payload;
    TransID createdBy;
    TransID removedBy;
};

static std::mutex dbMutex;

static std::unordered_map<
    RecordKey,
    std::list<DataVersion>
> versionStore;

bool isVisible(
    const DataVersion& dv,
    TransID snapshotAt,
    TransID reader) {

    bool writerVisible =
        (dv.createdBy == reader) ||
        (isCommitted(dv.createdBy) &&
         dv.createdBy < snapshotAt);

    if (!writerVisible)
        return false;

    if (dv.removedBy == 0)
        return true;

    bool deleterVisible =
        (dv.removedBy == reader) ||
        (isCommitted(dv.removedBy) &&
         dv.removedBy < snapshotAt);

    return !deleterVisible;
}

std::optional<std::string> fetchRecord(
    const RecordKey& key,
    TransID reader) {

    std::lock_guard lk(dbMutex);

    TransID snapshotAt;

    {
        std::lock_guard tLock(registryMtx);
        snapshotAt = transMap[reader].snapshotAt;
    }

    if (!versionStore.count(key))
        return std::nullopt;

    for (const auto& dv : versionStore[key]) {
        if (isVisible(dv, snapshotAt, reader))
            return dv.payload;
    }

    return std::nullopt;
}

void insertRecord(
    const RecordKey& key,
    const std::string& value,
    TransID txid) {

    std::lock_guard lk(dbMutex);

    versionStore[key].push_front({
        value,
        txid,
        0
    });
}

void modifyRecord(
    const RecordKey& key,
    const std::string& value,
    TransID txid) {

    std::lock_guard lk(dbMutex);

    TransID snapshotAt;

    {
        std::lock_guard tLock(registryMtx);
        snapshotAt = transMap[txid].snapshotAt;
    }

    if (versionStore.count(key)) {
        for (auto& dv : versionStore[key]) {
            if (isVisible(dv, snapshotAt, txid) &&
                dv.removedBy == 0) {

                dv.removedBy = txid;
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
    TransID txid) {

    std::lock_guard lk(dbMutex);

    TransID snapshotAt;

    {
        std::lock_guard tLock(registryMtx);
        snapshotAt = transMap[txid].snapshotAt;
    }

    if (!versionStore.count(key))
        return;

    for (auto& dv : versionStore[key]) {
        if (isVisible(dv, snapshotAt, txid) &&
            dv.removedBy == 0) {

            dv.removedBy = txid;
            return;
        }
    }
}

enum class AccessMode {
    READ,
    WRITE
};

struct LockRequest {
    TransID owner;
    AccessMode access;
    bool approved = false;
};

struct LockCell {
    std::list<LockRequest> pendingList;
    std::mutex mtx;
    std::condition_variable notifier;
};

static std::unordered_map<RecordKey, LockCell> lockMap;

static std::mutex graphMtx;

static std::unordered_map<
    TransID,
    std::unordered_set<TransID>
> waitForGraph;

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TransID txid)
        : std::runtime_error(
              "Deadlock detected for TXN " +
              std::to_string(txid)) {}
};

bool cycleDFS(
    TransID curr,
    std::unordered_map<TransID,
    std::unordered_set<TransID>>& g,
    std::unordered_set<TransID>& visited,
    std::unordered_set<TransID>& onStack) {

    visited.insert(curr);
    onStack.insert(curr);

    for (auto next : g[curr]) {

        if (!visited.count(next)) {
            if (cycleDFS(
                    next,
                    g,
                    visited,
                    onStack))
                return true;
        }

        if (onStack.count(next))
            return true;
    }

    onStack.erase(curr);
    return false;
}

bool detectCycle(
    TransID src,
    std::unordered_map<TransID,
    std::unordered_set<TransID>>& g) {

    std::unordered_set<TransID> visited;
    std::unordered_set<TransID> onStack;

    return cycleDFS(
        src,
        g,
        visited,
        onStack);
}

void grabLock(
    const RecordKey& key,
    TransID txid,
    AccessMode desired) {

    auto& cell = lockMap[key];

    std::unique_lock uLock(cell.mtx);

    cell.pendingList.push_back({
        txid,
        desired,
        false
    });

    auto mine =
        std::prev(cell.pendingList.end());

    while (true) {

        bool mustWait = false;
        std::unordered_set<TransID> blockers;

        for (auto& req : cell.pendingList) {

            if (&req == &(*mine))
                break;

            if (!req.approved)
                continue;

            if (desired == AccessMode::WRITE ||
                req.access == AccessMode::WRITE) {

                mustWait = true;
                blockers.insert(req.owner);
            }
        }

        if (!mustWait) {

            mine->approved = true;

            std::lock_guard gLock(graphMtx);
            waitForGraph.erase(txid);

            return;
        }

        {
            std::lock_guard gLock(graphMtx);

            waitForGraph[txid] = blockers;

            if (detectCycle(txid, waitForGraph)) {
                cell.pendingList.erase(mine);
                throw DeadlockException(txid);
            }
        }

        cell.notifier.wait(uLock);
    }
}

void unlockAll(TransID txid) {

    {
        std::lock_guard tLock(registryMtx);
        transMap[txid].unlocking = true;
    }

    for (auto& [key, lc] : lockMap) {

        std::lock_guard lock(lc.mtx);

        lc.pendingList.remove_if(
            [&](const LockRequest& req) {
                return req.owner == txid;
            });

        lc.notifier.notify_all();
    }

    std::lock_guard gLock(graphMtx);
    waitForGraph.erase(txid);
}

class TxnEngine {
public:

    TransID begin() {
        return startTxn();
    }

    std::optional<std::string> read(
        TransID txid,
        const RecordKey& key) {

        grabLock(key, txid, AccessMode::READ);
        return fetchRecord(key, txid);
    }

    void insert(
        TransID txid,
        const RecordKey& key,
        const std::string& value) {

        grabLock(key, txid, AccessMode::WRITE);
        insertRecord(key, value, txid);
    }

    void update(
        TransID txid,
        const RecordKey& key,
        const std::string& value) {

        grabLock(key, txid, AccessMode::WRITE);
        modifyRecord(key, value, txid);
    }

    void remove(
        TransID txid,
        const RecordKey& key) {

        grabLock(key, txid, AccessMode::WRITE);
        eraseRecord(key, txid);
    }

    void commit(TransID txid) {

        {
            std::lock_guard tLock(registryMtx);
            transMap[txid].state = TransState::COMMITTED;
        }

        unlockAll(txid);

        std::cout
            << "[TXN "
            << txid
            << "] COMMITTED\n";
    }

    void abort(TransID txid) {

        {
            std::lock_guard sLock(dbMutex);

            for (auto& [key, versions] : versionStore) {

                for (auto& dv : versions) {

                    if (dv.createdBy == txid)
                        dv.removedBy = txid;

                    if (dv.removedBy == txid)
                        dv.removedBy = 0;
                }
            }
        }

        {
            std::lock_guard tLock(registryMtx);
            transMap[txid].state = TransState::ABORTED;
        }

        unlockAll(txid);

        std::cout
            << "[TXN "
            << txid
            << "] ABORTED\n";
    }
};

void showValue(
    const std::optional<std::string>& res,
    TransID txid,
    const RecordKey& k) {

    std::cout
        << "[TXN "
        << txid
        << "] "
        << k
        << " = "
        << (res ? *res : "<invisible>")
        << '\n';
}

int main() {

    TxnEngine db;

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
