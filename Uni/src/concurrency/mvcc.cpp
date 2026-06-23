#include "concurrency/mvcc.h"
#include <iostream>
#include <algorithm>

void MVCCManager::BeginTx(TxId_t txid) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_txns_.insert(txid);
    if (txid >= next_txid_) {
        next_txid_ = txid + 1;
    }
}

void MVCCManager::CommitTx(TxId_t txid) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_txns_.erase(txid);
    committed_txns_.insert(txid);
}

void MVCCManager::AbortTx(TxId_t txid) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_txns_.erase(txid);
    aborted_txns_.insert(txid);
}

MVCCSnapshot MVCCManager::GetSnapshot(TxId_t txid) {
    std::lock_guard<std::mutex> lock(mutex_);
    MVCCSnapshot snap;
    snap.txid = txid;
    snap.watermark = next_txid_;
    snap.active_txns = active_txns_;
    return snap;
}

bool MVCCManager::IsVisible(const MVCCSnapshot& snapshot, TxId_t xmin, TxId_t xmax) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Evaluate visibility of the creator (xmin)
    bool xmin_visible = false;
    if (xmin == 0) {
        return false;
    }
    if (xmin == snapshot.txid) {
        xmin_visible = true; // We created it
    } else if (committed_txns_.find(xmin) != committed_txns_.end()) {
        // Created by committed transaction. Was it committed when snapshot was taken?
        // True if it was not in active list and is below watermark
        if (snapshot.active_txns.find(xmin) == snapshot.active_txns.end() && xmin < snapshot.watermark) {
            xmin_visible = true;
        }
    }

    if (!xmin_visible) {
        return false;
    }

    // 2. Evaluate visibility of the deleter (xmax)
    bool xmax_visible = false;
    if (xmax == 0) {
        xmax_visible = false; // Not deleted
    } else if (xmax == snapshot.txid) {
        xmax_visible = true; // Deleted by us, invisible
    } else if (committed_txns_.find(xmax) != committed_txns_.end()) {
        // Deleted by a committed transaction. Was it committed when snapshot was taken?
        if (snapshot.active_txns.find(xmax) == snapshot.active_txns.end() && xmax < snapshot.watermark) {
            xmax_visible = true; // Deletion is committed and visible, so tuple is invisible
        }
    }

    return !xmax_visible;
}

bool MVCCManager::CanWrite(TxId_t txid, TxId_t xmin, TxId_t xmax) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // If no delete watermark, it is editable
    if (xmax == 0) {
        return true;
    }

    // If deleted by us, we can modify it
    if (xmax == txid) {
        return true;
    }

    // If xmax is committed, someone else already updated or deleted it
    if (committed_txns_.find(xmax) != committed_txns_.end()) {
        return false;
    }

    // If xmax is active, someone is modifying it right now (write conflict)
    if (active_txns_.find(xmax) != active_txns_.end()) {
        return false;
    }

    return true;
}

void MVCCManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_txns_.clear();
    committed_txns_.clear();
    aborted_txns_.clear();
    next_txid_ = 1;
}
