#pragma once

#include "storage/page.h"
#include <unordered_set>
#include <mutex>
#include <vector>

struct MVCCSnapshot {
    TxId_t txid;
    TxId_t watermark; // The maximum transaction ID when the snapshot was taken
    std::unordered_set<TxId_t> active_txns; // Set of transaction IDs that were active (uncommitted)
};

class MVCCManager {
public:
    MVCCManager() = default;

    // Registers a transaction as active
    void BeginTx(TxId_t txid);

    // Marks a transaction as committed
    void CommitTx(TxId_t txid);

    // Marks a transaction as aborted
    void AbortTx(TxId_t txid);

    // Takes a snapshot for transaction txid
    MVCCSnapshot GetSnapshot(TxId_t txid);

    // Visibility rule check
    bool IsVisible(const MVCCSnapshot& snapshot, TxId_t xmin, TxId_t xmax) const;

    // Checks for write-write conflicts: returns true if the transaction can modify the record
    bool CanWrite(TxId_t txid, TxId_t xmin, TxId_t xmax) const;

    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_set<TxId_t> active_txns_;
    std::unordered_set<TxId_t> committed_txns_;
    std::unordered_set<TxId_t> aborted_txns_;
    TxId_t next_txid_ = 1;
};
