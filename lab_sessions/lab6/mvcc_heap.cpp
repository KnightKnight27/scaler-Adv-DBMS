#include "mvcc_heap.h"
#include <atomic>

std::atomic<TxID> g_next_xid{1};
std::mutex g_tx_mutex;
std::unordered_map<TxID, Transaction> g_transactions;

std::mutex g_heap_mutex;
std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

TxID begin_transaction() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid) || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_xid) || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid)) return v.value;
    }
    return std::nullopt;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;   
                break;
            }
        }
    }
    g_heap[key].push_front({new_value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}