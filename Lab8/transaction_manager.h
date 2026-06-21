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
    TxID id = 0;
    TxID snapshot = 0;
    TxStatus status = TxStatus::ACTIVE;
    bool shrinking = false;
    std::unordered_set<RowKey> held_locks;
};

struct RowVersion {
    std::string value;
    TxID xmin = 0;
    TxID xmax = 0;   // 0 => still live
};

struct LockState {
    std::unordered_set<TxID> shared_holders;
    TxID exclusive_holder = 0;
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
    TxID next_id_ = 1;
    std::mutex mutex_;
    std::condition_variable cv_;

    std::unordered_map<TxID, Transaction> txns_;
    std::unordered_map<RowKey, std::list<RowVersion>> rows_;
    std::unordered_map<RowKey, LockState> locks_;
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for_;

private:
    Transaction* get_txn(TxID xid);
    const Transaction* get_txn(TxID xid) const;

    bool is_visible(const RowVersion& v, const Transaction& reader) const;

    std::unordered_set<TxID> blockers_for(
        const LockState& state,
        TxID xid,
        LockMode mode
    ) const;

    bool has_cycle_from(TxID start) const;

    void acquire_lock(TxID xid, const RowKey& key, LockMode mode);

    void release_locks_unlocked(TxID xid);

    void rollback_unlocked(TxID xid);

    std::list<RowVersion>::iterator find_visible_version(
        const RowKey& key,
        const Transaction& reader
    );
};