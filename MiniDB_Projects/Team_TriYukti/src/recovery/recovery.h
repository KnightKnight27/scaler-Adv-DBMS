#pragma once
#include "recovery/log_manager.h"
#include "storage/buffer_pool.h"
#include <unordered_map>

namespace minidb {

class MVCCManager;

class RecoveryManager {
public:
    RecoveryManager(LogManager *log_manager, BufferPool *buffer_pool, MVCCManager *mvcc_manager);
    
    void Recover();

private:
    LogManager *log_manager_;
    BufferPool *buffer_pool_;
    MVCCManager *mvcc_manager_;
    
    std::unordered_map<txn_id_t, int32_t> active_txns_; 
    std::unordered_map<page_id_t, int32_t> dirty_pages_;
    
    void Analysis(const std::vector<LogRecord> &logs);
    void Redo(const std::vector<LogRecord> &logs);
    void Undo(const std::vector<LogRecord> &logs);
};

} // namespace minidb
