#pragma once

#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

using TxID = std::uint64_t;
using RowKey = std::string;

enum class TxStatus {
    ACTIVE,
    COMMITTED,
    ABORTED
};

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

struct Transaction {
    TxID id;
    TxID snapshotId;
    TxStatus status = TxStatus::ACTIVE;
    bool shrinking = false;
};

struct RowVersion {
    std::string value;
    TxID xmin;
    TxID xmax = 0;
};

struct LockState {
    std::unordered_set<TxID> sharedHolders;
    TxID exclusiveHolder = 0;
};

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid);
};

class TransactionManager {
public:
    TxID begin();
    std::optional<std::string> read(TxID xid, const RowKey& key);
    void insert(TxID xid, const RowKey& key, const std::string& value);
    void update(TxID xid, const RowKey& key, const std::string& value);
    void remove(TxID xid, const RowKey& key);
    void commit(TxID xid);
    void abort(TxID xid);

private:
    TxID nextId = 1;
    std::mutex mutex;
    std::condition_variable lockChanged;

    std::unordered_map<TxID, Transaction> transactions;
    std::unordered_map<RowKey, std::list<RowVersion>> rows;
    std::unordered_map<RowKey, LockState> locks;
    std::unordered_map<TxID, std::unordered_set<TxID>> waitsFor;

    void requireActive(TxID xid) const;
    bool isVisible(const RowVersion& version, const Transaction& reader) const;
    std::unordered_set<TxID> blockers(const LockState& state,
                                      TxID xid, LockMode mode) const;
    bool hasCycle(TxID start) const;
    void acquireLock(TxID xid, const RowKey& key, LockMode mode);
    void releaseLocks(TxID xid);
};
