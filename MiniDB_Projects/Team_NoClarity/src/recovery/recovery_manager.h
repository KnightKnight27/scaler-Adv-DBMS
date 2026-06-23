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

/**
 * Performs ARIES transactional crash recovery sequence: Analysis, Redo, and Undo.
 */
class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_manager, BufferPoolManager* bpm, LogManager* log_manager)
        : disk_manager_(disk_manager), bpm_(bpm), log_manager_(log_manager) {}
    ~RecoveryManager() = default;

    // Executes recovery protocol
    void RunRecovery();

    // Reconstructs transaction and dirty page table configurations
    void ExecuteAnalysisPhase(std::vector<txn_id_t>& active_txns, std::unordered_map<page_id_t, lsn_t>& dpt);
    
    // Re-applies logged updates matching DPT criteria
    void ExecuteRedoPhase(const std::unordered_map<page_id_t, lsn_t>& dpt);
    
    // Rolls back changes associated with aborted/loser transactions
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
