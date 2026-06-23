#include "mvcc_heap.h"

bool MvccHeap::is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) const {
    // The creating transaction must be either us, or committed before our snapshot.
    bool xmin_ok = (v.xmin == reader_xid) ||
                   (registry_.is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    if (v.xmax == kInvalidTxID) return true;

    // The version is invisible if its deleter is us, or committed before our snapshot.
    bool xmax_invisible = (v.xmax == reader_xid) ||
                          (registry_.is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

std::optional<std::string> MvccHeap::read(const RowKey& key, TxID xid) {
    TxID snap = registry_.snapshot_xid(xid);

    std::lock_guard<std::mutex> lk(mu_);
    auto it = heap_.find(key);
    if (it == heap_.end()) return std::nullopt;
    for (const auto& v : it->second) {
        if (is_visible(v, snap, xid)) return v.value;
    }
    return std::nullopt;
}

void MvccHeap::insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard<std::mutex> lk(mu_);
    heap_[key].push_front({value, xid, kInvalidTxID});
}

void MvccHeap::update(const RowKey& key, const std::string& new_value, TxID xid) {
    TxID snap = registry_.snapshot_xid(xid);

    std::lock_guard<std::mutex> lk(mu_);
    auto it = heap_.find(key);
    if (it != heap_.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == kInvalidTxID) {
                v.xmax = xid;   // mark the current version as superseded by us
                break;
            }
        }
    }
    heap_[key].push_front({new_value, xid, kInvalidTxID});
}

void MvccHeap::remove(const RowKey& key, TxID xid) {
    TxID snap = registry_.snapshot_xid(xid);

    std::lock_guard<std::mutex> lk(mu_);
    auto it = heap_.find(key);
    if (it == heap_.end()) return;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == kInvalidTxID) {
            v.xmax = xid;
            return;
        }
    }
}

void MvccHeap::rollback(TxID xid) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, chain] : heap_) {
        for (auto& v : chain) {
            if (v.xmin == xid) v.xmax = xid;          // self-inserted versions become dead
            if (v.xmax == xid) v.xmax = kInvalidTxID; // undo deletes/supersessions we made
        }
    }
}
