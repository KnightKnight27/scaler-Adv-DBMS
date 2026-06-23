#include "recovery/recovery_manager.h"

#include <unordered_set>

namespace minidb {

namespace {

void RemoveFromIndexes(Catalog* catalog, const TableSchema& schema, const std::string& table,
                       const Row& row) {
    if (auto* pk = schema.PrimaryKeyColumn()) {
        int pk_idx = schema.ColumnIndex(pk->name);
        if (auto* index = catalog->GetPrimaryIndex(table)) {
            index->Remove(row.Get(static_cast<std::size_t>(pk_idx)));
        }
    }
    for (const auto& col : schema.columns) {
        if (col.indexed && !col.primary_key) {
            if (auto* sec = catalog->GetSecondaryIndex(table, col.name)) {
                int idx = schema.ColumnIndex(col.name);
                sec->Remove(row.Get(static_cast<std::size_t>(idx)));
            }
        }
    }
}

void UpdateIndexes(Catalog* catalog, const TableSchema& schema, const std::string& table,
                   const Row& row, const Rid& rid) {
    if (auto* pk = schema.PrimaryKeyColumn()) {
        int pk_idx = schema.ColumnIndex(pk->name);
        if (auto* index = catalog->GetPrimaryIndex(table)) {
            index->Insert(row.Get(static_cast<std::size_t>(pk_idx)), rid);
        }
    }
    for (const auto& col : schema.columns) {
        if (col.indexed && !col.primary_key) {
            if (auto* sec = catalog->GetSecondaryIndex(table, col.name)) {
                int idx = schema.ColumnIndex(col.name);
                sec->Insert(row.Get(static_cast<std::size_t>(idx)), rid);
            }
        }
    }
}

bool RowExistsByPk(Catalog* catalog, const TableSchema& schema, const std::string& table,
                   const Row& row) {
    if (auto* pk = schema.PrimaryKeyColumn()) {
        if (auto* index = catalog->GetPrimaryIndex(table)) {
            int pk_idx = schema.ColumnIndex(pk->name);
            return index->Search(row.Get(static_cast<std::size_t>(pk_idx))).has_value();
        }
    }
    return false;
}

}  // namespace

RecoveryManager::RecoveryManager(Catalog* catalog, WriteAheadLog* wal)
    : catalog_(catalog), wal_(wal) {}

void RecoveryManager::Recover() {
    auto records = wal_->ReadAll();
    std::unordered_set<int> committed;
    for (const auto& rec : records) {
        if (rec.type == LogRecordType::COMMIT) committed.insert(rec.txn_id);
    }

    for (const auto& rec : records) {
        if (!committed.count(rec.txn_id)) continue;

        auto schema_opt = catalog_->GetTable(rec.table);
        if (!schema_opt) continue;
        const TableSchema& schema = *schema_opt;
        auto* heap = catalog_->GetHeapFile(rec.table);
        if (!heap) continue;

        if (rec.type == LogRecordType::INSERT) {
            if (RowExistsByPk(catalog_, schema, rec.table, rec.row)) continue;
            Rid rid = heap->InsertTuple(rec.row);
            UpdateIndexes(catalog_, schema, rec.table, rec.row, rid);
        } else if (rec.type == LogRecordType::DELETE_TUP) {
            if (auto row = heap->GetTuple(rec.rid)) {
                RemoveFromIndexes(catalog_, schema, rec.table, *row);
                heap->DeleteTuple(rec.rid);
                continue;
            }
            if (auto* pk = schema.PrimaryKeyColumn()) {
                int pk_idx = schema.ColumnIndex(pk->name);
                if (auto* index = catalog_->GetPrimaryIndex(rec.table)) {
                    if (auto rid = index->Search(rec.row.Get(static_cast<std::size_t>(pk_idx)))) {
                        if (auto row = heap->GetTuple(*rid)) {
                            RemoveFromIndexes(catalog_, schema, rec.table, *row);
                            heap->DeleteTuple(*rid);
                        }
                    }
                }
            }
        }
    }
}

void RecoveryManager::Checkpoint() {
    LogRecord rec;
    rec.type = LogRecordType::CHECKPOINT;
    wal_->Append(rec);
    wal_->Flush();
}

}  // namespace minidb
