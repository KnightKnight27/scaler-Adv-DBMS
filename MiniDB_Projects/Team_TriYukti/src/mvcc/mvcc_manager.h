#pragma once
#include "transaction/transaction.h"
#include "storage/page.h"
#include "storage/buffer_pool.h"
#include "sql/executor.h"
#include <vector>

namespace minidb {

constexpr txn_id_t TXN_NULL = -1;

class MVCCManager {
public:
    MVCCManager(BufferPool *buffer_pool);
    ~MVCCManager() = default;

    bool InsertVersion(const RecordId &rid, const Tuple &tuple, Transaction *txn);
    bool DeleteVersion(const RecordId &rid, Transaction *txn);
    
    bool ReadVisibleVersion(const RecordId &rid, Transaction *txn, Tuple *tuple);
    
    int Vacuum(int32_t min_active_ts, TableInfo &tinfo);

    void RecordCommit(txn_id_t txn_id, int32_t commit_ts) {
        commit_map_[txn_id] = commit_ts;
    }

private:
    BufferPool *buffer_pool_;
    std::unordered_map<txn_id_t, int32_t> commit_map_;
};

} // namespace minidb
