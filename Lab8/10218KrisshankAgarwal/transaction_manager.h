#pragma once

#include "mvcc_types.h"
#include "version_chain.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <iostream>

namespace mvcc {

//  Transaction State

enum class TxnState : uint8_t {
    ACTIVE    = 0,
    COMMITTED = 1,
    ABORTED   = 2
};

//  Write-Set Entry — tracks what a transaction wrote

struct WriteSetEntry {
    uint64_t                           logicalKey;
    std::shared_ptr<VersionRecord>     newVersion;
    std::shared_ptr<VersionRecord>     prevHead;   // old head (for rollback)
};

//  Transaction Context

struct Transaction {
    TxnID       id;
    Timestamp   beginTS;
    Timestamp   commitTS;          // set when committed
    TxnState    state;
    IsolationLevel isolationLevel;

    std::vector<WriteSetEntry> writeSet;
    std::unordered_set<uint64_t> readSet;   // for Serializable SSI

    Transaction(TxnID id_, Timestamp bts, IsolationLevel iso)
        : id(id_), beginTS(bts), commitTS(INF_TS),
          state(TxnState::ACTIVE), isolationLevel(iso) {}

    bool isActive()    const { return state == TxnState::ACTIVE; }
    bool isCommitted() const { return state == TxnState::COMMITTED; }
    bool isAborted()   const { return state == TxnState::ABORTED; }
};

//  Transaction Manager

class TransactionManager {
public:
    explicit TransactionManager(IsolationLevel defaultIso = IsolationLevel::SNAPSHOT)
        : nextTxnID_(1), globalTS_(0), defaultIso_(defaultIso) {}

    // ---- Begin / Commit / Abort ----

    std::shared_ptr<Transaction> begin(IsolationLevel iso) {
        TxnID id = nextTxnID_.fetch_add(1, std::memory_order_relaxed);
        Timestamp bts = globalTS_.fetch_add(1, std::memory_order_relaxed);
        auto txn = std::make_shared<Transaction>(id, bts, iso);
        {
            std::unique_lock lock(mu_);
            active_[id] = txn;
        }
        return txn;
    }

    std::shared_ptr<Transaction> begin() { return begin(defaultIso_); }

    // Returns the commit timestamp (or 0 on conflict / abort)
    Timestamp commit(std::shared_ptr<Transaction>& txn,
                     VersionChainIndex& vci) {
        if (!txn->isActive())
            throw TxnAbortedError("Transaction already ended");

        Timestamp cts = globalTS_.fetch_add(1, std::memory_order_relaxed);

        // Validate write-set: no write-write conflicts
        for (auto& entry : txn->writeSet) {
            auto chain = vci.get(entry.logicalKey);
            if (!chain) continue;
            auto head = chain->head();
            // If another txn wrote this key after our beginTS → conflict
            if (head && head != entry.newVersion && head->beginTS > txn->beginTS &&
                head->creatorTxn != txn->id) {
                abort(txn, vci);
                throw TxnConflictError("Write-write conflict on key " +
                                       std::to_string(entry.logicalKey));
            }
        }

        // Commit: seal all versions we wrote
        for (auto& entry : txn->writeSet) {
            entry.newVersion->status  = VersionStatus::COMMITTED;
            entry.newVersion->beginTS = cts;
            // Close the previous version's visibility window
            if (entry.prevHead &&
                entry.prevHead->status == VersionStatus::COMMITTED)
                entry.prevHead->endTS = cts;
        }

        txn->commitTS = cts;
        txn->state    = TxnState::COMMITTED;
        {
            std::unique_lock lock(mu_);
            active_.erase(txn->id);
        }
        updateHorizon();
        return cts;
    }

    void abort(std::shared_ptr<Transaction>& txn, VersionChainIndex& /*vci*/) {
        if (!txn->isActive()) return;
        // Mark all versions written as ABORTED
        for (auto& entry : txn->writeSet) {
            entry.newVersion->status = VersionStatus::ABORTED;
            entry.newVersion->endTS  = 0;
        }
        txn->state = TxnState::ABORTED;
        {
            std::unique_lock lock(mu_);
            active_.erase(txn->id);
        }
    }

    // Oldest active transaction's beginTS — used for GC horizon
    Timestamp gcHorizon() const {
        std::shared_lock lock(mu_);
        return horizon_;
    }

    size_t activeCount() const {
        std::shared_lock lock(mu_);
        return active_.size();
    }

    Timestamp currentTS() const {
        return globalTS_.load(std::memory_order_relaxed);
    }

private:
    void updateHorizon() {
        std::unique_lock lock(mu_);
        Timestamp h = globalTS_.load(std::memory_order_relaxed);
        for (auto& [id, txn] : active_)
            h = std::min(h, txn->beginTS);
        horizon_ = h;
    }

    std::atomic<TxnID>     nextTxnID_;
    std::atomic<Timestamp> globalTS_;
    IsolationLevel         defaultIso_;

    mutable std::shared_mutex mu_;
    std::unordered_map<TxnID, std::shared_ptr<Transaction>> active_;
    Timestamp horizon_ = 0;
};

} 