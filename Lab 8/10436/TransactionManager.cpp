#include "TransactionManager.h"

bool TransactionManager::is_active(TxnId id) const {
    auto it = txns.find(id);
    return it != txns.end() && it->second.state == TxnState::ACTIVE;
}

TxnId TransactionManager::begin() {
    TxnId id    = next_id++;
    Timestamp ts = now();
    txns[id]    = {id, ts, TxnState::ACTIVE};
    std::cout << "[BEGIN] T" << id << " started (snapshot ts=" << ts << ")\n";
    return id;
}

std::string TransactionManager::read(TxnId txn_id, const RecordKey& key) {
    if (!is_active(txn_id)) return "ABORTED";

    // Reads use the transaction's start timestamp for snapshot isolation
    Timestamp snap = txns[txn_id].start_ts;
    auto val = mvcc.read(key, snap);
    if (!val) return "NOT_FOUND";
    return *val;
}

std::string TransactionManager::write(TxnId txn_id, const RecordKey& key, const std::string& value) {
    if (!is_active(txn_id)) return "ABORTED";

    if (lock_mgr.try_acquire(txn_id, key, LockMode::EXCLUSIVE)) {
        mvcc.write(key, value, txn_id, now());
        return "OK";
    }

    // Lock blocked — check for deadlock
    std::vector<TxnId> holders = lock_mgr.get_holders(key);
    deadlock_det.update_wait_for(txn_id, holders);

    TxnId victim = deadlock_det.detect_cycle();
    if (victim != INVALID_TXN) {
        std::cout << "[DEADLOCK] Cycle detected. Aborting victim T" << victim << "\n";
        abort(victim);
        if (victim == txn_id) return "ABORTED";

        // Victim released its lock — retry
        if (lock_mgr.try_acquire(txn_id, key, LockMode::EXCLUSIVE)) {
            deadlock_det.remove_txn(txn_id);  // no longer waiting
            mvcc.write(key, value, txn_id, now());
            return "OK";
        }
    }

    return "BLOCKED";
}

void TransactionManager::commit(TxnId txn_id) {
    if (!is_active(txn_id)) return;
    txns[txn_id].state = TxnState::COMMITTED;
    lock_mgr.release_all(txn_id);
    deadlock_det.remove_txn(txn_id);
    std::cout << "[COMMIT] T" << txn_id << " committed\n";
}

void TransactionManager::abort(TxnId txn_id) {
    if (txns.find(txn_id) == txns.end()) return;
    if (txns[txn_id].state != TxnState::ACTIVE) return;
    txns[txn_id].state = TxnState::ABORTED;
    mvcc.abort_txn(txn_id);
    lock_mgr.release_all(txn_id);
    deadlock_det.remove_txn(txn_id);
    std::cout << "[ABORT] Transaction T" << txn_id << " aborted\n";
}

void TransactionManager::print_status() const {
    std::cout << "  Active transactions: ";
    for (const auto& [id, ctx] : txns)
        if (ctx.state == TxnState::ACTIVE)
            std::cout << "T" << id << " ";
    std::cout << "\n";
}
