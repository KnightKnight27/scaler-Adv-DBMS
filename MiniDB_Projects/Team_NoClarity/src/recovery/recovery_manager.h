#ifndef RECOVERY_MANAGER_H
#define RECOVERY_MANAGER_H

#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include <unordered_map>
#include <vector>

namespace minidb {

struct TransactionTableEntry {
    txn_id_t txn_id;
    lsn_t last_lsn;
    enum class TxnStatus { RUNNING, COMMITTED, ABORTED } status;
};

class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_manager, BufferPoolManager* bpm, LogManager* log_manager)
        : disk_manager_(disk_manager), bpm_(bpm), log_manager_(log_manager) {}
    ~RecoveryManager() = default;

    void RunRecovery();

    // Exposed for testing
    void ExecuteAnalysisPhase(std::vector<txn_id_t>& active_txns, std::unordered_map<page_id_t, lsn_t>& dpt);
    void ExecuteRedoPhase(const std::unordered_map<page_id_t, lsn_t>& dpt);
    void ExecuteUndoPhase(std::vector<txn_id_t>& active_txns);

private:
    DiskManager* disk_manager_;
    BufferPoolManager* bpm_;
    LogManager* log_manager_;
    
    std::unordered_map<txn_id_t, TransactionTableEntry> active_txns_table_;
    std::unordered_map<page_id_t, lsn_t> dpt_;
};

} // namespace minidb

#endif // RECOVERY_MANAGER_H
