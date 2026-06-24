#pragma once
/**
 * Lab 6 — Transaction Manager
 *
 * Orchestrates MVCC + Strict 2PL + Deadlock Detection.
 * Provides a unified API for transaction operations.
 */

#include "mvcc.h"
#include "lock_manager.h"
#include "deadlock_detector.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <optional>

// ─────────────────────────────────────────────────
// Transaction State
// ─────────────────────────────────────────────────
enum class TxnState { ACTIVE, COMMITTED, ABORTED };

inline std::string txn_state_str(TxnState s) {
    switch (s) {
        case TxnState::ACTIVE:    return "ACTIVE";
        case TxnState::COMMITTED: return "COMMITTED";
        case TxnState::ABORTED:   return "ABORTED";
    }
    return "UNKNOWN";
}

struct TransactionInfo {
    TxnId    id;
    TxnState state;
    int      reads = 0;
    int      writes = 0;
    int      locks_acquired = 0;
};

// ─────────────────────────────────────────────────
// TransactionManager: Unified concurrency control
// ─────────────────────────────────────────────────
class TransactionManager {
private:
    MVCCManager       mvcc_;
    LockManager       lock_mgr_;
    DeadlockDetector  deadlock_;
    std::mutex        mutex_;

    std::unordered_map<TxnId, TransactionInfo> transactions_;
    bool verbose_ = true;

    void log(const std::string& msg) {
        if (verbose_) {
            std::cout << "  [TxnMgr] " << msg << std::endl;
        }
    }

public:
    void set_verbose(bool v) { verbose_ = v; }

    /**
     * BEGIN — Start a new transaction
     */
    TxnId begin() {
        TxnId txn_id = mvcc_.begin_transaction();

        TransactionInfo info;
        info.id = txn_id;
        info.state = TxnState::ACTIVE;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            transactions_[txn_id] = info;
        }

        log("BEGIN T" + std::to_string(txn_id));
        return txn_id;
    }

    /**
     * READ — Read a row (acquires shared lock, then reads via MVCC)
     */
    std::optional<std::string> read(TxnId txn_id, RowId row_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = transactions_.find(txn_id);
        if (it == transactions_.end() || it->second.state != TxnState::ACTIVE) {
            log("ERROR: T" + std::to_string(txn_id) + " is not active");
            return std::nullopt;
        }

        // Acquire shared lock (Strict 2PL)
        auto result = lock_mgr_.lock(txn_id, row_id, LockMode::SHARED);

        if (result == LockManager::LockResult::WAITING) {
            // Build wait-for edges
            auto blockers = lock_mgr_.get_blocking_txns(txn_id);
            for (TxnId blocker : blockers) {
                deadlock_.add_edge(txn_id, blocker);
            }

            // Check for deadlock
            std::vector<TxnId> cycle;
            if (deadlock_.detect(cycle)) {
                TxnId victim = deadlock_.choose_victim(cycle);
                log("DEADLOCK detected! Cycle involves: " + cycle_to_string(cycle));
                log("Aborting victim T" + std::to_string(victim));

                // Abort the victim (release its locks)
                abort_internal(victim);
                if (victim == txn_id) return std::nullopt;

                // Retry lock
                result = lock_mgr_.lock(txn_id, row_id, LockMode::SHARED);
            }
        }

        if (result == LockManager::LockResult::GRANTED ||
            result == LockManager::LockResult::UPGRADED) {
            it->second.reads++;
            it->second.locks_acquired++;
        }

        // Read via MVCC (snapshot isolation)
        auto value = mvcc_.read(txn_id, row_id);

        log("READ T" + std::to_string(txn_id) + " row=" + std::to_string(row_id) +
            " → " + (value ? "\"" + *value + "\"" : "NULL"));

        return value;
    }

    /**
     * WRITE — Write a row (acquires exclusive lock, then writes via MVCC)
     */
    bool write(TxnId txn_id, RowId row_id, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = transactions_.find(txn_id);
        if (it == transactions_.end() || it->second.state != TxnState::ACTIVE) {
            log("ERROR: T" + std::to_string(txn_id) + " is not active");
            return false;
        }

        // Acquire exclusive lock (Strict 2PL)
        auto result = lock_mgr_.lock(txn_id, row_id, LockMode::EXCLUSIVE);

        if (result == LockManager::LockResult::WAITING) {
            auto blockers = lock_mgr_.get_blocking_txns(txn_id);
            for (TxnId blocker : blockers) {
                deadlock_.add_edge(txn_id, blocker);
            }

            std::vector<TxnId> cycle;
            if (deadlock_.detect(cycle)) {
                TxnId victim = deadlock_.choose_victim(cycle);
                log("DEADLOCK detected! Cycle: " + cycle_to_string(cycle));
                log("Aborting victim T" + std::to_string(victim));

                abort_internal(victim);
                if (victim == txn_id) return false;

                result = lock_mgr_.lock(txn_id, row_id, LockMode::EXCLUSIVE);
            }
        }

        if (result == LockManager::LockResult::GRANTED ||
            result == LockManager::LockResult::UPGRADED) {
            it->second.writes++;
            it->second.locks_acquired++;
        }

        // Write via MVCC (creates new version)
        mvcc_.write(txn_id, row_id, value);

        log("WRITE T" + std::to_string(txn_id) + " row=" + std::to_string(row_id) +
            " value=\"" + value + "\"");

        return true;
    }

    /**
     * COMMIT — Commit a transaction (release all locks)
     */
    bool commit(TxnId txn_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = transactions_.find(txn_id);
        if (it == transactions_.end() || it->second.state != TxnState::ACTIVE) {
            return false;
        }

        it->second.state = TxnState::COMMITTED;
        mvcc_.commit(txn_id);
        lock_mgr_.release_all(txn_id);     // Strict 2PL: release at commit
        deadlock_.remove_transaction(txn_id);

        log("COMMIT T" + std::to_string(txn_id) +
            " (reads=" + std::to_string(it->second.reads) +
            ", writes=" + std::to_string(it->second.writes) + ")");

        return true;
    }

    /**
     * ABORT — Abort a transaction (undo writes, release locks)
     */
    bool abort(TxnId txn_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return abort_internal(txn_id);
    }

    // ─── Inspection ───

    void print_transaction_table() const {
        std::cout << "\n  Transaction Table:" << std::endl;
        std::cout << "  " << std::string(55, '-') << std::endl;
        std::cout << "  | TxnID |   State   | Reads | Writes | Locks |" << std::endl;
        std::cout << "  " << std::string(55, '-') << std::endl;
        for (const auto& [id, info] : transactions_) {
            std::cout << "  | T" << std::setw(4) << id
                      << " | " << std::setw(9) << txn_state_str(info.state)
                      << " | " << std::setw(5) << info.reads
                      << " | " << std::setw(6) << info.writes
                      << " | " << std::setw(5) << info.locks_acquired
                      << " |" << std::endl;
        }
        std::cout << "  " << std::string(55, '-') << std::endl;
    }

    void print_lock_table() {
        lock_mgr_.print_lock_table();
    }

    void print_wait_for_graph() {
        deadlock_.print_graph();
    }

    void print_version_chain(RowId row_id) {
        mvcc_.print_version_chain(row_id);
    }

private:
    bool abort_internal(TxnId txn_id) {
        auto it = transactions_.find(txn_id);
        if (it == transactions_.end()) return false;

        it->second.state = TxnState::ABORTED;
        mvcc_.abort(txn_id);
        lock_mgr_.release_all(txn_id);
        deadlock_.remove_transaction(txn_id);

        log("ABORT T" + std::to_string(txn_id));
        return true;
    }

    std::string cycle_to_string(const std::vector<TxnId>& cycle) const {
        std::string s;
        for (size_t i = 0; i < cycle.size(); i++) {
            if (i > 0) s += " → ";
            s += "T" + std::to_string(cycle[i]);
        }
        return s;
    }
};
