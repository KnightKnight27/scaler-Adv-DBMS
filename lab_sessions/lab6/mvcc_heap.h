#ifndef MVCC_HEAP_H
#define MVCC_HEAP_H

#include "common.h"
#include <string>
#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>

struct RowVersion {
    std::string value;
    TxID        xmin;   
    TxID        xmax;   
};

extern std::mutex g_tx_mutex;
extern std::unordered_map<TxID, Transaction> g_transactions;
extern std::mutex g_heap_mutex;
extern std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

TxID begin_transaction();
bool is_committed(TxID xid);
bool is_aborted(TxID xid);

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid);
std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid);
void mvcc_insert(const RowKey& key, const std::string& value, TxID xid);
void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid);
void mvcc_delete(const RowKey& key, TxID xid);

#endif