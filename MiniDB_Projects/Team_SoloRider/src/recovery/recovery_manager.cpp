#include "recovery/recovery_manager.h"
#include <unordered_set>

namespace minidb {

RecoveryManager::RecoveryManager(WriteAheadLog* wal, std::unordered_map<std::string, HeapFile*> tables)
    : wal_(wal), tables_(tables) {}

void RecoveryManager::recover() {
    auto records = wal_->read_all();
    std::unordered_set<txn_id_t> committed_txns;
    
    // Pass 1: Analysis (Find committed txns)
    for (const auto& rec : records) {
        if (rec.type == LogRecordType::COMMIT) {
            committed_txns.insert(rec.txn_id);
        }
    }
    
    // Pass 2: Redo (Only for committed txns for simplicity - assumes No-Steal)
    for (const auto& rec : records) {
        if (committed_txns.count(rec.txn_id)) {
            // Very simplified: assuming single table "default"
            if (tables_.count("default") == 0) continue;
            HeapFile* hf = tables_["default"];
            
            if (rec.type == LogRecordType::INSERT) {
                // In a real system, we'd insert exactly at rec.rid, but insert_tuple finds free space.
                // We'd need a specific `insert_at(rid, data)` method. We'll skip for this simple demo.
            } else if (rec.type == LogRecordType::DELETE) {
                hf->delete_tuple(rec.rid);
            }
        }
    }
}

} // namespace minidb
