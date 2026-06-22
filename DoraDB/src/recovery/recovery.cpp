#include "recovery/recovery.h"
#include <iostream>
#include <cstring>

RecoveryManager::RecoveryResult RecoveryManager::Recover(
    WAL& wal,
    std::unordered_map<std::string, TableAccess>& tables) {

    RecoveryResult result;
    auto records = wal.ReadAll();
    if (records.empty()) return result;

    std::cout << "[Recovery] Scanning " << records.size() << " WAL records...\n";

    // ---- Phase 1: Analysis ----
    // Determine which txns committed and which are still active
    std::unordered_set<int> committed, aborted, active;
    for (auto& r : records) {
        switch (r.type) {
            case WALRecordType::BEGIN:    active.insert(r.txn_id); break;
            case WALRecordType::COMMIT:   committed.insert(r.txn_id); active.erase(r.txn_id); break;
            case WALRecordType::ABORT:    aborted.insert(r.txn_id); active.erase(r.txn_id); break;
            default: break;
        }
    }
    result.committed_txns = committed.size();
    result.aborted_txns = active.size();  // active at crash = need undo

    std::cout << "[Recovery] Committed: " << committed.size()
              << ", Need undo: " << active.size() << "\n";

    // ---- Phase 2: Redo (forward scan) ----
    // Replay all operations from committed transactions
    for (auto& r : records) {
        if (!committed.count(r.txn_id)) continue;
        auto tit = tables.find(r.table_name);
        if (tit == tables.end()) continue;
        auto& ta = tit->second;

        switch (r.type) {
            case WALRecordType::INSERT:
                // Re-insert the row
                ta.heap->InsertRow(r.after_image.data(), r.after_image.size());
                if (ta.index) {
                    Row row = DeserializeRow(r.after_image.data(), r.after_image.size(), ta.schema);
                    if (ta.schema.pk_index >= 0)
                        ta.index->Insert(row[ta.schema.pk_index].int_val, r.rid);
                }
                result.redo_count++;
                break;

            case WALRecordType::DELETE_REC:
                ta.heap->DeleteRow(r.rid);
                if (ta.index && ta.schema.pk_index >= 0) {
                    Row row = DeserializeRow(r.before_image.data(), r.before_image.size(), ta.schema);
                    ta.index->Delete(row[ta.schema.pk_index].int_val);
                }
                result.redo_count++;
                break;

            case WALRecordType::UPDATE_REC:
                ta.heap->UpdateRow(r.rid, r.after_image.data(), r.after_image.size());
                result.redo_count++;
                break;

            default: break;
        }
    }
    std::cout << "[Recovery] Redo: " << result.redo_count << " operations replayed\n";

    // ---- Phase 3: Undo (backward scan) ----
    // Reverse operations from uncommitted (active) transactions
    for (int i = records.size() - 1; i >= 0; i--) {
        auto& r = records[i];
        if (!active.count(r.txn_id)) continue;
        auto tit = tables.find(r.table_name);
        if (tit == tables.end()) continue;
        auto& ta = tit->second;

        switch (r.type) {
            case WALRecordType::INSERT:
                // Undo insert = delete
                ta.heap->DeleteRow(r.rid);
                if (ta.index && ta.schema.pk_index >= 0) {
                    Row row = DeserializeRow(r.after_image.data(), r.after_image.size(), ta.schema);
                    ta.index->Delete(row[ta.schema.pk_index].int_val);
                }
                result.undo_count++;
                break;

            case WALRecordType::DELETE_REC:
                // Undo delete = re-insert
                ta.heap->InsertRow(r.before_image.data(), r.before_image.size());
                result.undo_count++;
                break;

            case WALRecordType::UPDATE_REC:
                // Undo update = restore before image
                ta.heap->UpdateRow(r.rid, r.before_image.data(), r.before_image.size());
                result.undo_count++;
                break;

            default: break;
        }
    }
    std::cout << "[Recovery] Undo: " << result.undo_count << " operations reversed\n";

    return result;
}
