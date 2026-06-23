#include "recovery/RecoveryManager.hpp"

#include <set>

using namespace std;

namespace minidb {

RecoveryManager::RecoveryManager(WAL& wal, Catalog& catalog) : wal_(wal), catalog_(catalog) {}

void RecoveryManager::redo(const LogRecord& rec) {
    if (rec.type != LogRecordType::INSERT) return;
    HeapFile* hf = catalog_.getHeapFile(rec.table);
    BPlusTree* idx = catalog_.getPrimaryIndex(rec.table);
    const TableDef* def = catalog_.getTable(rec.table);
    if (!hf) return;
    hf->insertRow(rec.row);
    if (idx && def && !def->primary_key_column.empty()) {
        auto pk = rec.row.find(def->primary_key_column);
        if (pk != rec.row.end()) idx->insert(atoi(pk->second.c_str()), rec.location);
    }
}

void RecoveryManager::undo(const LogRecord& rec) {
    if (rec.type == LogRecordType::INSERT) {
        HeapFile* hf = catalog_.getHeapFile(rec.table);
        BPlusTree* idx = catalog_.getPrimaryIndex(rec.table);
        const TableDef* def = catalog_.getTable(rec.table);
        if (hf) hf->deleteRow(rec.location);
        if (idx && def) {
            auto pk = rec.row.find(def->primary_key_column);
            if (pk != rec.row.end()) idx->remove(atoi(pk->second.c_str()));
        }
    }
}

void RecoveryManager::recover() {
    auto records = wal_.readAll();
    set<int> committed;
    for (const LogRecord& rec : records)
        if (rec.type == LogRecordType::COMMIT) committed.insert(rec.txn_id);

    for (const LogRecord& rec : records)
        if ((rec.type == LogRecordType::INSERT || rec.type == LogRecordType::DELETE) && committed.count(rec.txn_id))
            redo(rec);

    for (auto it = records.rbegin(); it != records.rend(); ++it)
        if ((it->type == LogRecordType::INSERT || it->type == LogRecordType::DELETE) && !committed.count(it->txn_id))
            undo(*it);
}

}  // namespace minidb
