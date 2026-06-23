#ifndef MINIDB_TRANSACTION_H
#define MINIDB_TRANSACTION_H

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

/**
 * Transaction state machine for Strict Two-Phase Locking (S2PL).
 *
 * ═══════════════════════════════════════════════════════════════════════
 * STATE MACHINE:
 *
 *   GROWING ──lock()──▶ GROWING    (acquire more locks)
 *   GROWING ──commit──▶ COMMITTED  (release ALL locks at once)
 *   GROWING ──abort───▶ ABORTED    (release ALL locks, undo changes)
 *
 * KEY PROPERTY — STRICT 2PL:
 *   All locks are held until COMMIT or ABORT. There is no SHRINKING
 *   phase where locks are released one at a time. This guarantees
 *   SERIALIZABLE isolation — the strongest isolation level.
 *
 * WHY STRICT 2PL (not basic 2PL)?
 *   Basic 2PL allows releasing locks before commit, which can cause
 *   cascading aborts. If Txn A releases a lock, Txn B reads the value,
 *   and then Txn A aborts — Txn B read "dirty" data and must also abort.
 *   Strict 2PL prevents this entirely by holding all locks until end.
 *
 * COMPARISON WITH MVCC:
 *   - 2PL: readers block writers, writers block readers
 *   - MVCC: readers never block writers (they read old versions)
 *   - Trade-off: 2PL gives strong correctness guarantees but lower
 *     throughput under contention. MVCC gives higher read throughput
 *     but is more complex to implement (Track B extension).
 * ═══════════════════════════════════════════════════════════════════════
 */
enum class TxnState {
    GROWING,     // Acquiring locks, doing work
    COMMITTED,   // Successfully completed, all locks released
    ABORTED      // Rolled back, all locks released
};

/**
 * Represents a single database transaction.
 *
 * Tracks the transaction's state, acquired locks, and undo information
 * for rollback on abort.
 */
class Transaction {
public:
    /** Undo entry for rollback. */
    struct UndoEntry {
        std::string tableName;
        int recordId;
        int32_t oldId;
        int32_t oldVal;
        bool wasInsert;  // true if this operation was an INSERT (undo = delete)
    };

    explicit Transaction(int txnId)
        : txnId_(txnId), state_(TxnState::GROWING) {}

    int getTxnId() const { return txnId_; }
    TxnState getState() const { return state_; }

    void setState(TxnState s) { state_ = s; }

    // ── Lock tracking ───────────────────────────────────────────────

    /** Record that this transaction holds a shared lock on rid. */
    void addSharedLock(int64_t rid) { sharedLocks_.insert(rid); }

    /** Record that this transaction holds an exclusive lock on rid. */
    void addExclusiveLock(int64_t rid) { exclusiveLocks_.insert(rid); }

    /** Check if this transaction holds any lock on rid. */
    bool holdsLock(int64_t rid) const {
        return sharedLocks_.count(rid) > 0 || exclusiveLocks_.count(rid) > 0;
    }

    bool holdsSharedLock(int64_t rid) const {
        return sharedLocks_.count(rid) > 0;
    }

    bool holdsExclusiveLock(int64_t rid) const {
        return exclusiveLocks_.count(rid) > 0;
    }

    const std::unordered_set<int64_t>& getSharedLocks() const {
        return sharedLocks_;
    }

    const std::unordered_set<int64_t>& getExclusiveLocks() const {
        return exclusiveLocks_;
    }

    // ── Undo log ────────────────────────────────────────────────────

    /** Record an undo entry for rollback. */
    void addUndoEntry(const UndoEntry& entry) {
        undoLog_.push_back(entry);
    }

    /** Get all undo entries (in insertion order — reverse for undo). */
    const std::vector<UndoEntry>& getUndoLog() const {
        return undoLog_;
    }

    std::string stateToString() const {
        switch (state_) {
            case TxnState::GROWING:   return "GROWING";
            case TxnState::COMMITTED: return "COMMITTED";
            case TxnState::ABORTED:   return "ABORTED";
        }
        return "UNKNOWN";
    }

private:
    int txnId_;
    TxnState state_;
    std::unordered_set<int64_t> sharedLocks_;
    std::unordered_set<int64_t> exclusiveLocks_;
    std::vector<UndoEntry> undoLog_;
};

#endif // MINIDB_TRANSACTION_H
