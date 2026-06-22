#include "recovery/recovery_manager.h"

namespace minidb {

RecoveryStats RecoveryManager::recover() {
  RecoveryStats stats;
  vector<LogRecord> records = log_->readAll();
  if (records.empty()) return stats;

  // --- Step 2: classify transactions ---
  unordered_set<txn_id_t> committed;
  for (const auto &r : records) {
    if (r.type == LogType::kCommit) { committed.insert(r.txn); stats.committed_txns++; }
    if (r.type == LogType::kAbort)  { stats.aborted_txns++; }
  }

  // Helper: resolve a table name to its heap (may be null if table was dropped).
  auto heap_of = [&](const string &name) -> TableHeap * {
    TableInfo *t = catalog_->getTable(name);
    return t ? t->heap.get() : nullptr;
  };

  // --- Step 3: REDO all data changes in log order ---
  for (const auto &r : records) {
    TableHeap *h = nullptr;
    if (r.type == LogType::kInsert || r.type == LogType::kDelete) h = heap_of(r.table);
    if (h == nullptr) continue;
    if (r.type == LogType::kInsert) { h->recoverInsert(r.rid, r.tuple_bytes); stats.redone++; }
    else if (r.type == LogType::kDelete) { h->recoverDelete(r.rid); stats.redone++; }
  }

  // --- Step 4: UNDO losers in reverse log order ---
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    const LogRecord &r = *it;
    if (committed.count(r.txn)) continue;               // winners stay
    TableHeap *h = heap_of(r.table);
    if (h == nullptr) continue;
    if (r.type == LogType::kInsert) { h->recoverDelete(r.rid); stats.undone++; }
    else if (r.type == LogType::kDelete) { h->recoverInsert(r.rid, r.tuple_bytes); stats.undone++; }
  }

  // --- Step 5: persist the recovered heaps to disk ---
  // (Indexes are rebuilt by the caller once recovery is complete.)
  bpm_->flushAll();
  return stats;
}

}  // namespace minidb
