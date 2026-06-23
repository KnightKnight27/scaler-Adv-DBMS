#include "RecoveryManager.h"

#include <iostream>
#include <unordered_set>

// ── Constructor ─────────────────────────────────────────────────────────

RecoveryManager::RecoveryManager(
    LogManager* logManager,
    std::unordered_map<int, Table*>& tables)
    : logManager_(logManager), tables_(tables) {}

// ── Main recovery entry point ───────────────────────────────────────────

int RecoveryManager::recover() {
    std::cout << "\n=== CRASH RECOVERY STARTED ===" << std::endl;

    std::vector<LogRecord> logs = logManager_->readAllLogs();

    if (logs.empty()) {
        std::cout << "WAL is empty — nothing to recover." << std::endl;
        return 0;
    }

    std::cout << "Read " << logs.size() << " log records from WAL." << std::endl;

    // ── Phase 1: REDO ───────────────────────────────────────────────
    std::cout << "\n--- REDO Phase (forward scan) ---" << std::endl;
    redoPhase(logs);

    // ── Phase 2: UNDO ───────────────────────────────────────────────
    std::cout << "\n--- UNDO Phase (backward scan) ---" << std::endl;
    undoPhase(logs);

    std::cout << "\n=== RECOVERY COMPLETE ===" << std::endl;
    std::cout << "Committed transactions preserved: " << committedTxns_.size() << std::endl;
    std::cout << "Loser transactions undone: " << undoneTxns_.size() << std::endl;

    return static_cast<int>(undoneTxns_.size());
}

// ── REDO Phase ──────────────────────────────────────────────────────────

void RecoveryManager::redoPhase(const std::vector<LogRecord>& logs) {
    /*
     * REDO: Scan forward through the WAL and re-apply ALL data operations.
     *
     * We redo EVERYTHING, even uncommitted transactions, because:
     *   1. Some committed pages may not have been flushed to disk
     *   2. Redo is idempotent (applying the same change twice is safe)
     *   3. We'll undo the uncommitted ones in the next phase
     *
     * This is the "repeat history" paradigm of ARIES: first bring the
     * database to exactly the state it was in at crash time, then
     * selectively undo the losers.
     */
    int redoCount = 0;

    for (const auto& rec : logs) {
        switch (rec.type) {
            case LogType::INSERT:
            case LogType::UPDATE:
            case LogType::DELETE_OP:
                redoRecord(rec);
                redoCount++;
                break;

            case LogType::BEGIN:
                std::cout << "  REDO: Txn " << rec.txnId << " BEGIN" << std::endl;
                break;

            case LogType::COMMIT:
                std::cout << "  REDO: Txn " << rec.txnId << " COMMIT" << std::endl;
                break;

            case LogType::ABORT:
                std::cout << "  REDO: Txn " << rec.txnId << " ABORT" << std::endl;
                break;
        }
    }

    std::cout << "  Redid " << redoCount << " data operations." << std::endl;
}

// ── UNDO Phase ──────────────────────────────────────────────────────────

void RecoveryManager::undoPhase(const std::vector<LogRecord>& logs) {
    /*
     * UNDO: Identify "loser" transactions (BEGIN but no COMMIT/ABORT)
     * and reverse their operations.
     *
     * Step 1: Build the set of active transactions at crash time
     *   - BEGIN → add to active set
     *   - COMMIT/ABORT → remove from active set
     *   - Anything still in the active set = loser
     *
     * Step 2: Walk the WAL BACKWARDS and undo each loser's operations
     */

    // Step 1: Find losers
    std::unordered_set<int> activeTxns;

    for (const auto& rec : logs) {
        switch (rec.type) {
            case LogType::BEGIN:
                activeTxns.insert(rec.txnId);
                break;
            case LogType::COMMIT:
                activeTxns.erase(rec.txnId);
                committedTxns_.push_back(rec.txnId);
                break;
            case LogType::ABORT:
                activeTxns.erase(rec.txnId);
                break;
            default:
                break;
        }
    }

    if (activeTxns.empty()) {
        std::cout << "  No loser transactions found." << std::endl;
        return;
    }

    std::cout << "  Loser transactions: ";
    for (int txnId : activeTxns) {
        std::cout << txnId << " ";
        undoneTxns_.push_back(txnId);
    }
    std::cout << std::endl;

    // Step 2: Walk backwards and undo loser operations
    int undoCount = 0;

    for (int i = static_cast<int>(logs.size()) - 1; i >= 0; i--) {
        const auto& rec = logs[i];

        // Only undo operations from loser transactions
        if (activeTxns.count(rec.txnId) == 0) continue;

        switch (rec.type) {
            case LogType::INSERT:
                // Undo INSERT → delete the record (set to tombstone)
                std::cout << "  UNDO INSERT: Txn " << rec.txnId
                          << " record " << rec.recordId << std::endl;
                undoRecord(rec);
                undoCount++;
                break;

            case LogType::UPDATE:
                // Undo UPDATE → restore old values
                std::cout << "  UNDO UPDATE: Txn " << rec.txnId
                          << " record " << rec.recordId
                          << " restore (" << rec.oldId << "," << rec.oldVal << ")"
                          << std::endl;
                undoRecord(rec);
                undoCount++;
                break;

            case LogType::DELETE_OP:
                // Undo DELETE → restore old record
                std::cout << "  UNDO DELETE: Txn " << rec.txnId
                          << " record " << rec.recordId
                          << " restore (" << rec.oldId << "," << rec.oldVal << ")"
                          << std::endl;
                undoRecord(rec);
                undoCount++;
                break;

            default:
                break;
        }
    }

    std::cout << "  Undid " << undoCount << " operations from "
              << activeTxns.size() << " loser transaction(s)." << std::endl;
}

// ── Record-level redo/undo ──────────────────────────────────────────────

void RecoveryManager::redoRecord(const LogRecord& rec) {
    auto it = tables_.find(rec.tableId);
    if (it == tables_.end()) return;  // table doesn't exist (shouldn't happen)

    Table* table = it->second;

    switch (rec.type) {
        case LogType::INSERT: {
            // Re-apply the insert: write new values at the recorded position
            Record newRec(rec.newId, rec.newVal);
            table->updateRecord(rec.recordId, newRec);
            break;
        }
        case LogType::UPDATE: {
            // Re-apply the update: overwrite with new values
            Record newRec(rec.newId, rec.newVal);
            table->updateRecord(rec.recordId, newRec);
            break;
        }
        case LogType::DELETE_OP: {
            // Re-apply the delete: write tombstone
            Record tombstone;
            tombstone.markDeleted();
            table->updateRecord(rec.recordId, tombstone);
            break;
        }
        default:
            break;
    }
}

void RecoveryManager::undoRecord(const LogRecord& rec) {
    auto it = tables_.find(rec.tableId);
    if (it == tables_.end()) return;

    Table* table = it->second;

    switch (rec.type) {
        case LogType::INSERT: {
            // Undo INSERT: delete the inserted record (tombstone)
            Record tombstone;
            tombstone.markDeleted();
            table->updateRecord(rec.recordId, tombstone);
            break;
        }
        case LogType::UPDATE: {
            // Undo UPDATE: restore old values
            Record oldRec(rec.oldId, rec.oldVal);
            table->updateRecord(rec.recordId, oldRec);
            break;
        }
        case LogType::DELETE_OP: {
            // Undo DELETE: restore the old record
            Record oldRec(rec.oldId, rec.oldVal);
            table->updateRecord(rec.recordId, oldRec);
            break;
        }
        default:
            break;
    }
}
