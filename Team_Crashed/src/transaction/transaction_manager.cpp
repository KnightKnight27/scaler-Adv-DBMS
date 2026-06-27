// =============================================================================
// src/transaction/transaction_manager.cpp
// -----------------------------------------------------------------------------
// Transaction lifecycle: begin / commit / abort.
//
// MVCC visibility
// ---------------
//   A row carries (createdBy, deletedBy). A reader's snapshot S is the
//   committed high-water mark at the reader's BEGIN time: S = the highest
//   txn id that had COMMITTED before the reader started. A row is visible
//   to the reader iff:
//
//       createdBy == reader          (own write: visible unless the reader
//                                     itself also deleted it), OR
//       createdBy <= S                (creator committed before the reader
//                                     began) AND
//       (deletedBy == 0 OR deletedBy > S)   (not yet deleted at snapshot)
//
//   The "own write" arm lets a transaction see the rows it inserted even
//   though its own id is greater than its snapshot. The S = committed-high
//   rule (not "id - 1") is what makes an uncommitted txn's writes invisible
//   to a reader that began after it — that is the snapshot-isolation
//   guarantee the v1 "id - 1" snapshot failed to provide.
//
// Write-write conflict detection
// -------------------------------
//   At commit time, if any other committed txn that started before the
//   committing txn's snapshot wrote the same rid, the commit returns
//   TXN_CONFLICT. The executor then aborts.
// =============================================================================
#include "transaction/transaction_manager.h"

#include <algorithm>

namespace minidb::transaction {

TransactionManager::TransactionManager()  = default;
TransactionManager::~TransactionManager() = default;

// Allocate a new transaction id, record the snapshot high-water mark
// (the highest COMMITTED txn id before this one), and return the id.
TransactionId TransactionManager::begin() {
    std::lock_guard<std::mutex> lk(mu_);

    TransactionId id = next_++;
    auto t = std::make_unique<Transaction>(id);
    // Snapshot = highest txn id that has COMMITTED so far. Rows created by
    // any txn that has not yet committed (even one that began earlier) are
    // therefore invisible to this reader — the snapshot-isolation guarantee.
    t->setSnapshotHigh(committedHighWater_);
    txns_[id] = std::move(t);
    return id;
}

// Commit a transaction.
//   1. Check for write-write conflicts.
//   2. If clean, mark COMMITTED, advance the committed high-water mark,
//      release 2PL locks, drop write set.
Status TransactionManager::commit(TransactionId id) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = txns_.find(id);
    if (it == txns_.end()) return Status::NOT_FOUND;

    if (hasConflictLocked(id)) return Status::TXN_CONFLICT;

    it->second->setState(TxnState::COMMITTED);
    // This txn is now durably visible to every reader that begins later:
    // advance the committed high-water mark.
    if (id > committedHighWater_) committedHighWater_ = id;
    auto wIt = writes_.find(id);
    if (wIt != writes_.end()) {
        for (RecordId rid : wIt->second) {
            committedWrites_[rid] = id;
        }
    }
    lockMgr_.releaseAll(id);
    writes_.erase(id);
    return Status::OK;
}

// Abort a transaction. Mark ABORTED, release 2PL locks, drop write set.
Status TransactionManager::abort(TransactionId id) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = txns_.find(id);
    if (it == txns_.end()) return Status::NOT_FOUND;

    it->second->setState(TxnState::ABORTED);
    lockMgr_.releaseAll(id);
    writes_.erase(id);
    return Status::OK;
}

// MVCC visibility test.
//
// A row (created by c, deleted by d) is visible to reader r iff:
//   - it is r's OWN write (c == r.id): visible unless r itself deleted it
//     (d == r.id); OR
//   - the creator committed before r's snapshot (c <= r.snapshotHigh()) AND
//     the row was not deleted before r's snapshot
//     (d == 0 OR d > r.snapshotHigh()).
//
// d == 0 means "not deleted yet". The own-write arm is what lets a txn read
// its own uncommitted inserts.
bool TransactionManager::isVisible(TransactionId rowCreator,
                                   TransactionId rowDeleter,
                                   const Transaction& reader) {
    const TransactionId self = reader.id();
    if (rowCreator == self) {
        // Own write: visible unless the reader itself deleted it.
        return !(rowDeleter != 0 && rowDeleter == self);
    }
    if (rowCreator > reader.snapshotHigh()) return false;          // not committed at snapshot
    if (rowDeleter != 0 && rowDeleter <= reader.snapshotHigh()) return false;  // deleted at snapshot
    return true;
}

// Look up the Transaction object for an id. Returns nullptr if the id is
// unknown or the txn has been evicted from the active set.
const Transaction* TransactionManager::getTransaction(TransactionId id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = txns_.find(id);
    return it == txns_.end() ? nullptr : it->second.get();
}

// Record that txn wrote rid (for MVCC conflict detection at commit time).
void TransactionManager::recordWrite(TransactionId txn, RecordId rid) {
    std::lock_guard<std::mutex> lk(mu_);
    writes_[txn].insert(rid);
}

// Return true if any other committed txn (with id < txn's snapshotHigh
// + 1, i.e. that started before or concurrently with txn) also wrote one
// of the same rids. This is the first-updater-wins rule: if there is an
// overlap, the committing txn loses.
bool TransactionManager::hasConflict(TransactionId txn) {
    std::lock_guard<std::mutex> lk(mu_);
    return hasConflictLocked(txn);
}

bool TransactionManager::hasConflictLocked(TransactionId txn) const {
    auto myIt = writes_.find(txn);
    if (myIt == writes_.end()) return false;

    auto txnIt = txns_.find(txn);
    if (txnIt == txns_.end()) return false;

    TransactionId mySnapshot = txnIt->second->snapshotHigh();

    for (RecordId rid : myIt->second) {
        auto committedIt = committedWrites_.find(rid);
        if (committedIt != committedWrites_.end() &&
            committedIt->second >= mySnapshot) {
            return true;
        }
    }

    for (const auto& [otherId, otherRids] : writes_) {
        if (otherId == txn) continue;

        // Only txns that could be visible in our snapshot matter.
        // An "older" txn (id <= snapshot) that committed already wrote
        // these rids before us -> conflict.
        if (otherId > mySnapshot) continue;

        auto oIt = txns_.find(otherId);
        if (oIt == txns_.end()) continue;
        if (oIt->second->state() != TxnState::COMMITTED) continue;

        for (RecordId rid : myIt->second) {
            if (otherRids.count(rid)) return true;
        }
    }
    return false;
}

// Return all transaction ids that are currently ACTIVE. Used at recovery
// time to know which txns need aborting after a crash.
std::unordered_set<TransactionId> TransactionManager::activeTxns() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::unordered_set<TransactionId> out;
    for (const auto& [id, t] : txns_) {
        if (t->state() == TxnState::ACTIVE) out.insert(id);
    }
    return out;
}

} // namespace minidb::transaction
