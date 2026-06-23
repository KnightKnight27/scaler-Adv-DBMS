#ifndef MINIDB_RECOVERY_MANAGER_H
#define MINIDB_RECOVERY_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "LogManager.h"
#include "Table.h"

/**
 * RecoveryManager implements ARIES-style crash recovery for MiniDB.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * ARIES RECOVERY ALGORITHM (simplified):
 *
 * On startup after a crash, recovery has TWO phases:
 *
 * PHASE 1 — REDO (forward scan):
 *   Scan the WAL from beginning to end. For every INSERT, UPDATE, or
 *   DELETE log record, re-apply the operation using the NEW values.
 *   This ensures all changes (committed or not) are reflected in the
 *   data files.
 *
 *   WHY redo uncommitted changes? Because some committed changes might
 *   not have been flushed to disk before the crash (the BufferPool may
 *   have had dirty pages that weren't written). By redoing everything,
 *   we guarantee the data files are up-to-date.
 *
 * PHASE 2 — UNDO (backward scan):
 *   Identify transactions that have a BEGIN record but NO COMMIT record.
 *   These are "loser" transactions that were active at crash time.
 *   For each loser, walk their log records in REVERSE order and undo
 *   each operation using the OLD values.
 *
 *   This restores the database to a consistent state where only
 *   committed changes are visible.
 *
 * FULL ARIES vs OUR SIMPLIFICATION:
 * Real ARIES has three phases (Analysis, Redo, Undo) and uses:
 *   - Dirty Page Table (DPT) to know which pages need redo
 *   - Active Transaction Table (ATT) to know which txns need undo
 *   - CLR (Compensation Log Records) to make undo operations idempotent
 *   - Checkpoints to limit the amount of redo work
 *
 * We simplify by:
 *   - Always redo everything from the beginning (no checkpoints)
 *   - Not writing CLRs during undo (acceptable for our workload)
 *   - Using the COMMIT/ABORT records to identify losers
 * ═══════════════════════════════════════════════════════════════════════
 */
class RecoveryManager {
public:
    /**
     * @param logManager  The WAL log manager
     * @param tables      Map of tableId → Table* for applying redo/undo
     */
    RecoveryManager(LogManager* logManager,
                    std::unordered_map<int, Table*>& tables);

    /**
     * Perform crash recovery.
     *
     * Called on database startup. Reads the WAL, redoes all operations,
     * then undoes loser transactions.
     *
     * Returns the number of transactions that were undone.
     */
    int recover();

    /** Get the list of recovered (undone) transaction IDs. */
    const std::vector<int>& getUndoneTransactions() const {
        return undoneTxns_;
    }

    /** Get the list of committed transaction IDs found during recovery. */
    const std::vector<int>& getCommittedTransactions() const {
        return committedTxns_;
    }

private:
    void redoPhase(const std::vector<LogRecord>& logs);
    void undoPhase(const std::vector<LogRecord>& logs);

    void redoRecord(const LogRecord& rec);
    void undoRecord(const LogRecord& rec);

    LogManager* logManager_;
    std::unordered_map<int, Table*>& tables_;

    std::vector<int> undoneTxns_;
    std::vector<int> committedTxns_;
};

#endif // MINIDB_RECOVERY_MANAGER_H
