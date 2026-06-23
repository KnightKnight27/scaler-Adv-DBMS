#pragma once
#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include "deadlock_detector.h"
#include "lock_manager.h"
#include "mvcc.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class TransactionState { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TransactionID tx_id;
    TransactionState state;
    TransactionID timestamp;
    std::set<std::string> read_set;
    std::set<std::string> write_set;

    Transaction(TransactionID id, TransactionID ts)
        : tx_id(id), state(TransactionState::ACTIVE), timestamp(ts) {}
};

class TransactionManager {
private:
    std::unordered_map<std::string, VersionChain> data;
    LockManager lock_manager;
    DeadlockDetector deadlock_detector;
    std::unordered_map<TransactionID, std::shared_ptr<Transaction>> transactions;
    TransactionID next_tx_id;
    TransactionID global_timestamp;

public:
    TransactionManager() : next_tx_id(1), global_timestamp(0) {}

    TransactionID beginTransaction();
    bool read(TransactionID tx_id, const std::string &key, std::string &value);
    bool write(TransactionID tx_id, const std::string &key, const std::string &value);
    bool commit(TransactionID tx_id);
    bool abort(TransactionID tx_id);

    void printVersionChains() const;
    void printTransactionState() const;
};

#endif // TRANSACTION_MANAGER_H
