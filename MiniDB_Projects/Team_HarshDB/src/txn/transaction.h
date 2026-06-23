#pragma once
// ---------------------------------------------------------------------------
// transaction.h - transaction bookkeeping + MVCC visibility.
//
// This is the integrated, encapsulated version of the Lab 6 transaction
// manager. Every transaction gets a monotonically increasing id and a snapshot
// (the id it was born with). A row version (xmin, xmax) is visible to a reader
// if it was created by a committed transaction at or before the snapshot and was
// not deleted by a committed transaction at or before the snapshot.
//
//   visible(v) :=  (v.xmin == me OR (committed(v.xmin) AND v.xmin < snapshot))
//              AND (v.xmax == 0  OR  v.xmax == me  OR  NOT committed-before-snapshot(v.xmax))
//
// That single rule is what lets readers see a stable snapshot without locking,
// which is the heart of the MVCC extension track.
// ---------------------------------------------------------------------------
#include "../common.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>

namespace minidb {

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxId     id = INVALID_TX;
    TxId     snapshot = 0;
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;        // 2PL phase flag
    std::vector<RID> inserted;            // for abort rollback
    std::vector<RID> deleted;             // RIDs whose xmax we stamped
};

class TransactionManager {
public:
    TxId begin() {
        std::lock_guard<std::mutex> lk(mu_);
        TxId id = next_xid_.fetch_add(1);
        txns_[id] = Transaction{id, id, TxStatus::ACTIVE, false, {}, {}};
        return id;
    }

    Transaction* get(TxId id) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = txns_.find(id);
        return it == txns_.end() ? nullptr : &it->second;
    }

    void set_committed(TxId id) {
        std::lock_guard<std::mutex> lk(mu_);
        if (txns_.count(id)) txns_[id].status = TxStatus::COMMITTED;
    }
    void set_aborted(TxId id) {
        std::lock_guard<std::mutex> lk(mu_);
        if (txns_.count(id)) txns_[id].status = TxStatus::ABORTED;
    }

    bool is_committed(TxId id) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = txns_.find(id);
        return it != txns_.end() && it->second.status == TxStatus::COMMITTED;
    }

    TxId snapshot_of(TxId id) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = txns_.find(id);
        return it == txns_.end() ? 0 : it->second.snapshot;
    }

    // The MVCC visibility test described in the header comment.
    bool visible(TxId xmin, TxId xmax, TxId reader) {
        TxId snap = snapshot_of(reader);
        bool xmin_ok = (xmin == reader) || (is_committed(xmin) && xmin < snap);
        if (!xmin_ok) return false;
        if (xmax == INVALID_TX) return true;
        bool xmax_kills = (xmax == reader) || (is_committed(xmax) && xmax < snap);
        return !xmax_kills;
    }

    TxId current_high_water() { return next_xid_.load(); }

    // ---- used by crash recovery ----
    // Re-register a transaction that the WAL says committed before the crash,
    // so that rows it created/deleted are once again treated as committed.
    void register_committed(TxId id) {
        std::lock_guard<std::mutex> lk(mu_);
        txns_[id] = Transaction{id, id, TxStatus::COMMITTED, true, {}, {}};
    }
    void ensure_next_above(TxId id) {
        TxId want = id + 1;
        TxId cur = next_xid_.load();
        while (want > cur && !next_xid_.compare_exchange_weak(cur, want)) { /* retry */ }
    }

private:
    std::atomic<TxId>                       next_xid_{1};
    std::mutex                              mu_;
    std::unordered_map<TxId, Transaction>   txns_;
};

} // namespace minidb
