#pragma once

#include "transaction/lock_manager.h"
#include "common/types.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>

/**
 * @enum TxStatus
 * @brief Represents the lifecycle status of a database transaction.
 */
enum class TxStatus { 
    ACTIVE, 
    COMMITTED, 
    ABORTED 
};

/**
 * @class TxManager
 * @brief Manages the transaction lifecycle, wrapping the LockManager to enforce concurrency controls.
 */
class TxManager {
public:
    /**
     * @brief Construct a Transaction Manager.
     */
    explicit TxManager(LockManager& lm);

    // Disable copy semantics
    TxManager(const TxManager&) = delete;
    TxManager& operator=(const TxManager&) = delete;

    /**
     * @brief Starts a new transaction, allocating and returning a unique TxID.
     */
    TxID begin();

    /**
     * @brief Acquires a shared lock on resource_key for reading.
     */
    void lockRead(TxID xid, const std::string& resource_key);

    /**
     * @brief Acquires an exclusive lock on resource_key for writing/updating.
     */
    void lockWrite(TxID xid, const std::string& resource_key);

    /**
     * @brief Commits a transaction: changes status and releases all held locks.
     */
    void commit(TxID xid);

    /**
     * @brief Aborts a transaction: sets status and releases locks to rollback changes.
     */
    void abort(TxID xid);

    /**
     * @brief Returns true if transaction xid is currently ACTIVE.
     */
    bool isActive(TxID xid) const;

    /**
     * @brief Returns the status of transaction xid.
     */
    TxStatus status(TxID xid) const;

    /**
     * @brief Helper returning a formatted row lock resource key ("table:key").
     */
    static std::string rowKey(const std::string& table, int32_t key) {
        return table + ":" + std::to_string(key);
    }

    /**
     * @brief Helper returning a formatted table-level lock resource key ("TABLE:table").
     */
    static std::string tableKey(const std::string& table) {
        return "TABLE:" + table;
    }

private:
    LockManager& lm_;
    std::atomic<TxID> next_xid_{1};        ///< Monotonically increasing Transaction ID counter
    mutable std::mutex tx_mutex_;          ///< Protects tx_table_
    std::unordered_map<TxID, TxStatus> tx_table_; ///< Global transaction status map
};
