#include "mvcc_heap.h"
#include <stdexcept>

MvccHeap& MvccHeap::getInstance() {
    static MvccHeap instance;
    return instance;
}

TxID MvccHeap::beginTransaction() {
    std::lock_guard<std::mutex> lk(tx_mutex);
    TxID xid = next_xid.fetch_add(1);
    transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool MvccHeap::isCommitted(TxID xid) {
    std::lock_guard<std::mutex> lk(tx_mutex);
    auto it = transactions.find(xid);
    return it != transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool MvccHeap::isAborted(TxID xid) {
    std::lock_guard<std::mutex> lk(tx_mutex);
    auto it = transactions.find(xid);
    return it != transactions.end() && it->second.status == TxStatus::ABORTED;
}

void MvccHeap::commitTransaction(TxID xid) {
    std::lock_guard<std::mutex> lk(tx_mutex);
    auto it = transactions.find(xid);
    if (it != transactions.end()) {
        it->second.status = TxStatus::COMMITTED;
    }
}

void MvccHeap::abortTransaction(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(heap_mutex);
        // Rollback all version edits by this transaction
        for (auto& [key, list] : heap) {
            for (auto& version : list) {
                if (version.xmin == xid) {
                    // Own insertions: invalidate them
                    version.xmax = xid; 
                }
                if (version.xmax == xid) {
                    // Own deletions/updates: restore old version
                    version.xmax = 0;
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(tx_mutex);
        auto it = transactions.find(xid);
        if (it != transactions.end()) {
            it->second.status = TxStatus::ABORTED;
        }
    }
}

bool MvccHeap::checkShrinking(TxID xid) {
    std::lock_guard<std::mutex> lk(tx_mutex);
    auto it = transactions.find(xid);
    if (it != transactions.end()) {
        return it->second.in_shrinking;
    }
    return false;
}

void MvccHeap::setShrinking(TxID xid) {
    std::lock_guard<std::mutex> lk(tx_mutex);
    auto it = transactions.find(xid);
    if (it != transactions.end()) {
        it->second.in_shrinking = true;
    }
}

bool MvccHeap::isVisible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // xmin check: created by us or committed before our snapshot started
    bool xmin_ok = (v.xmin == reader_xid) || (isCommitted(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    // xmax check: not deleted, or deleted by someone not yet committed or after our snapshot started
    if (v.xmax == 0) return true;
    
    bool xmax_deleted = (v.xmax == reader_xid) || (isCommitted(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_deleted;
}

std::optional<std::string> MvccHeap::readKey(const RowKey& key, TxID reader_xid) {
    TxID snap;
    {
        std::lock_guard<std::mutex> lk(tx_mutex);
        snap = transactions.at(reader_xid).snapshot_xid;
    }

    std::lock_guard<std::mutex> lk(heap_mutex);
    auto it = heap.find(key);
    if (it == heap.end()) return std::nullopt;

    for (const auto& version : it->second) {
        if (isVisible(version, snap, reader_xid)) {
            return version.value;
        }
    }
    return std::nullopt;
}

void MvccHeap::insertKey(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard<std::mutex> lk(heap_mutex);
    heap[key].push_front({value, xid, 0});
}

void MvccHeap::updateKey(const RowKey& key, const std::string& value, TxID xid) {
    TxID snap;
    {
        std::lock_guard<std::mutex> lk(tx_mutex);
        snap = transactions.at(xid).snapshot_xid;
    }

    std::lock_guard<std::mutex> lk(heap_mutex);
    auto it = heap.find(key);
    if (it != heap.end()) {
        for (auto& version : it->second) {
            if (isVisible(version, snap, xid) && version.xmax == 0) {
                version.xmax = xid; // logically delete old version
                break;
            }
        }
    }
    heap[key].push_front({value, xid, 0});
}

void MvccHeap::deleteKey(const RowKey& key, TxID xid) {
    TxID snap;
    {
        std::lock_guard<std::mutex> lk(tx_mutex);
        snap = transactions.at(xid).snapshot_xid;
    }

    std::lock_guard<std::mutex> lk(heap_mutex);
    auto it = heap.find(key);
    if (it == heap.end()) return;

    for (auto& version : it->second) {
        if (isVisible(version, snap, xid) && version.xmax == 0) {
            version.xmax = xid; // logically delete
            return;
        }
    }
}
