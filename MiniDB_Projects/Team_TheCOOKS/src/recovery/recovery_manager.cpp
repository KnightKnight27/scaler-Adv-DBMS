#include "recovery/recovery_manager.h"

#include <unordered_set>

#include "catalog/table.h"
#include "catalog/tuple.h"

namespace walterdb {

RecoveryManager::Stats RecoveryManager::run(const std::vector<LogRecord>& records) {
  Stats stats;

  // --- Analysis: which transactions committed? ---
  std::unordered_set<txn_id_t> committed;
  std::unordered_set<txn_id_t> seen;
  for (const LogRecord& r : records) {
    seen.insert(r.txn);
    if (r.type == LogType::Commit) committed.insert(r.txn);
  }
  stats.committed_txns = committed.size();
  stats.loser_txns = seen.size() - committed.size();

  auto pk_of = [](Table* t, const Tuple& row) -> const Value& {
    return row.value(static_cast<size_t>(t->info()->pk_column));
  };

  // --- Redo: replay committed data ops forward (idempotent). ---
  for (const LogRecord& r : records) {
    if (r.type != LogType::Insert && r.type != LogType::Delete) continue;
    if (!committed.count(r.txn)) continue;
    Table* t = catalog_->open_table_by_id(r.table_id);
    if (!t || t->info()->pk_column < 0) continue;
    Tuple row = Tuple::decode(t->schema(), r.row_image);
    if (r.type == LogType::Insert) {
      t->upsert(row);
    } else {
      t->delete_by_pk(pk_of(t, row));
    }
    ++stats.redone;
  }

  // --- Undo: reverse-replay losers, removing uncommitted changes. ---
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    const LogRecord& r = *it;
    if (r.type != LogType::Insert && r.type != LogType::Delete) continue;
    if (committed.count(r.txn)) continue;
    Table* t = catalog_->open_table_by_id(r.table_id);
    if (!t || t->info()->pk_column < 0) continue;
    Tuple row = Tuple::decode(t->schema(), r.row_image);
    if (r.type == LogType::Insert) {
      t->delete_by_pk(pk_of(t, row));  // undo an insert
    } else {
      t->upsert(row);                  // undo a delete
    }
    ++stats.undone;
  }

  return stats;
}

}  // namespace walterdb
