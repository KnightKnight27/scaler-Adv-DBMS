#pragma once
/**
 * Lab 6 — MVCC (Multi-Version Concurrency Control)
 *
 * Implements version chains with snapshot isolation.
 * Each write creates a new version; reads see a consistent snapshot.
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <optional>
#include <iostream>
#include <iomanip>
#include <algorithm>

using TxnId = uint64_t;
using RowId = uint64_t;

// ─────────────────────────────────────────────────
// Version: A single version of a row
// ─────────────────────────────────────────────────
struct Version {
    TxnId       created_by;    // transaction that created this version
    TxnId       deleted_by;    // transaction that deleted/replaced this version (0 = active)
    std::string value;         // the row's data
    Version*    prev;          // pointer to previous version (version chain)

    Version(TxnId creator, const std::string& val, Version* previous = nullptr)
        : created_by(creator), deleted_by(0), value(val), prev(previous) {}
};

// ─────────────────────────────────────────────────
// Snapshot: Represents a transaction's view of the database
// ─────────────────────────────────────────────────
struct Snapshot {
    TxnId              txn_id;
    TxnId              min_active;      // lowest active txn at snapshot time
    TxnId              max_txn;         // highest txn ID at snapshot time
    std::vector<TxnId> active_txns;     // list of active txns at snapshot time

    bool is_visible(const Version* ver) const {
        // A version is visible if:
        // 1. It was created by a committed transaction that started before our snapshot
        // 2. It was NOT deleted by a committed transaction before our snapshot
        // 3. Or it was created by our own transaction

        // Created by us
        if (ver->created_by == txn_id) {
            // But if we also deleted it, it's not visible
            return ver->deleted_by == 0 || ver->deleted_by != txn_id;
        }

        // Created by a transaction that was active when we took the snapshot → not visible
        for (TxnId active : active_txns) {
            if (ver->created_by == active) return false;
        }

        // Created by a future transaction → not visible
        if (ver->created_by > max_txn) return false;

        // Created by a committed transaction before our snapshot → visible
        // But only if not deleted by a committed transaction before our snapshot
        if (ver->deleted_by == 0) return true;  // not deleted

        // Deleted by us → not visible
        if (ver->deleted_by == txn_id) return false;

        // Deleted by a future transaction → still visible to us
        if (ver->deleted_by > max_txn) return true;

        // Deleted by an active transaction → still visible to us
        for (TxnId active : active_txns) {
            if (ver->deleted_by == active) return true;
        }

        // Deleted by a committed transaction → not visible
        return false;
    }
};

// ─────────────────────────────────────────────────
// MVCCManager: Manages version chains and snapshots
// ─────────────────────────────────────────────────
class MVCCManager {
private:
    std::unordered_map<RowId, Version*>  latest_versions_;  // row_id → latest version
    std::mutex                           mutex_;
    TxnId                                next_txn_id_ = 1;
    std::vector<TxnId>                   active_txns_;
    std::unordered_map<TxnId, Snapshot>  snapshots_;

public:
    ~MVCCManager() {
        // Clean up all version chains
        for (auto& [rid, ver] : latest_versions_) {
            while (ver) {
                Version* prev = ver->prev;
                delete ver;
                ver = prev;
            }
        }
    }

    // Begin a new transaction and take a snapshot
    TxnId begin_transaction() {
        std::lock_guard<std::mutex> lock(mutex_);
        TxnId txn_id = next_txn_id_++;

        Snapshot snap;
        snap.txn_id = txn_id;
        snap.min_active = active_txns_.empty() ? txn_id : active_txns_.front();
        snap.max_txn = txn_id - 1;
        snap.active_txns = active_txns_;

        active_txns_.push_back(txn_id);
        snapshots_[txn_id] = snap;

        return txn_id;
    }

    // Read a row — traverse version chain to find visible version
    std::optional<std::string> read(TxnId txn_id, RowId row_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = latest_versions_.find(row_id);
        if (it == latest_versions_.end()) return std::nullopt;

        auto snap_it = snapshots_.find(txn_id);
        if (snap_it == snapshots_.end()) return std::nullopt;

        const Snapshot& snap = snap_it->second;

        // Traverse version chain from newest to oldest
        Version* ver = it->second;
        while (ver) {
            if (snap.is_visible(ver)) {
                return ver->value;
            }
            ver = ver->prev;
        }

        return std::nullopt;  // no visible version
    }

    // Write a new version of a row
    bool write(TxnId txn_id, RowId row_id, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        Version* old_latest = nullptr;
        auto it = latest_versions_.find(row_id);
        if (it != latest_versions_.end()) {
            old_latest = it->second;
            // Mark old version as deleted by this transaction
            old_latest->deleted_by = txn_id;
        }

        // Create new version at head of chain
        Version* new_ver = new Version(txn_id, value, old_latest);
        latest_versions_[row_id] = new_ver;

        return true;
    }

    // Delete a row (mark latest visible version as deleted)
    bool remove(TxnId txn_id, RowId row_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = latest_versions_.find(row_id);
        if (it == latest_versions_.end()) return false;

        it->second->deleted_by = txn_id;
        return true;
    }

    // Commit a transaction
    void commit(TxnId txn_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_txns_.erase(
            std::remove(active_txns_.begin(), active_txns_.end(), txn_id),
            active_txns_.end()
        );
        snapshots_.erase(txn_id);
    }

    // Abort a transaction — undo all writes
    void abort(TxnId txn_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Remove versions created by this transaction
        for (auto& [rid, ver] : latest_versions_) {
            if (ver && ver->created_by == txn_id) {
                Version* to_delete = ver;
                ver = ver->prev;
                if (ver) ver->deleted_by = 0;  // un-delete previous version
                to_delete->prev = nullptr;
                delete to_delete;
            } else if (ver && ver->deleted_by == txn_id) {
                ver->deleted_by = 0;  // un-delete
            }
        }

        active_txns_.erase(
            std::remove(active_txns_.begin(), active_txns_.end(), txn_id),
            active_txns_.end()
        );
        snapshots_.erase(txn_id);
    }

    // Debug: print version chain for a row
    void print_version_chain(RowId row_id) const {
        auto it = latest_versions_.find(row_id);
        if (it == latest_versions_.end()) {
            std::cout << "  Row " << row_id << ": (not found)" << std::endl;
            return;
        }

        std::cout << "  Row " << row_id << " version chain:" << std::endl;
        Version* ver = it->second;
        int idx = 0;
        while (ver) {
            std::cout << "    [v" << idx << "] created_by=T" << ver->created_by
                      << " deleted_by=" << (ver->deleted_by ? "T" + std::to_string(ver->deleted_by) : "none")
                      << " value=\"" << ver->value << "\"" << std::endl;
            ver = ver->prev;
            idx++;
        }
    }
};
