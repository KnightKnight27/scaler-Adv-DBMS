#include "recovery_manager.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../storage/heap_file.h"
#include "../storage/heap_page.h"

namespace minidb {

namespace {
std::vector<LogRecord> ReadAllRecords(DiskManager *dm) {
  std::vector<LogRecord> records;
  auto in = dm->OpenLogForReading();
  if (!in.is_open()) return records;
  while (true) {
    int32_t len;
    in.read(reinterpret_cast<char *>(&len), sizeof(len));
    if (!in) break;
    std::string buf(len, '\0');
    in.read(buf.data(), len);
    if (!in) break;
    LogRecord rec;
    size_t pos = 0;
    if (LogRecord::Deserialize(buf, &pos, &rec)) records.push_back(rec);
  }
  return records;
}
}  // namespace

int RecoveryManager::Recover() {
  std::vector<LogRecord> records = ReadAllRecords(disk_manager_);
  if (records.empty()) return 0;

  std::unordered_set<txn_id_t> committed, aborted, began;
  std::unordered_map<txn_id_t, std::vector<const LogRecord *>> ops_by_txn;
  for (auto &r : records) {
    switch (r.type) {
      case LogType::BEGIN: began.insert(r.txn_id); break;
      case LogType::COMMIT: committed.insert(r.txn_id); break;
      case LogType::ABORT: aborted.insert(r.txn_id); break;
      case LogType::INSERT:
      case LogType::DELETE: ops_by_txn[r.txn_id].push_back(&r); break;
    }
  }

  // --- Redo: reapply every INSERT/DELETE in log order, idempotently. ---
  // Track the highest page_id touched so we can fast-forward DiskManager's
  // allocator past it before rebuilding indexes below - otherwise a fresh
  // BPlusTree::CreateEmpty() could allocate a page_id that collides with a
  // heap page that's referenced here but was never flushed before the
  // crash (so the on-disk file size alone doesn't account for it).
  int redo_count = 0;
  page_id_t max_page_seen = INVALID_PAGE_ID;
  for (auto &name : catalog_->TableNames()) {
    TableInfo *t = catalog_->GetTable(name);
    max_page_seen = std::max(max_page_seen, t->heap_first_page);
    max_page_seen = std::max(max_page_seen, t->btree_root);
  }
  for (auto &r : records) max_page_seen = std::max(max_page_seen, r.rid.page_id);
  disk_manager_->EnsureCapacity(max_page_seen + 1);

  for (auto &r : records) {
    if (r.type != LogType::INSERT && r.type != LogType::DELETE) continue;
    Page *page = bpm_->FetchPage(r.rid.page_id);
    HeapPage hp(page->GetData());
    if (hp.FreeEnd() == 0) hp.Init();  // page never made it to disk before the crash

    if (r.type == LogType::INSERT) {
      if (hp.NumSlots() == r.rid.slot_num) {
        hp.InsertTuple(r.data.data(), static_cast<int32_t>(r.data.size()));
        redo_count++;
      }
    } else {
      if (r.rid.slot_num < hp.NumSlots()) hp.DeleteTuple(r.rid.slot_num);
      redo_count++;
    }
    bpm_->UnpinPage(r.rid.page_id, true);
  }

  // --- Undo: losers are txns with a BEGIN but no COMMIT/ABORT. ---
  for (txn_id_t id : began) {
    if (committed.count(id) || aborted.count(id)) continue;
    auto &ops = ops_by_txn[id];
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
      const LogRecord *r = *it;
      Page *page = bpm_->FetchPage(r->rid.page_id);
      HeapPage hp(page->GetData());
      if (r->type == LogType::INSERT) {
        hp.DeleteTuple(r->rid.slot_num);
      } else {
        hp.RestoreTuple(r->rid.slot_num, static_cast<int32_t>(r->data.size()));
      }
      bpm_->UnpinPage(r->rid.page_id, true);
    }
  }

  // --- Rebuild every primary-key index from the now-correct heap data. ---
  for (auto &name : catalog_->TableNames()) {
    TableInfo *t = catalog_->GetTable(name);
    int pk_idx = t->schema.PrimaryKeyIndex();
    if (pk_idx < 0) continue;
    page_id_t new_root = BPlusTree::CreateEmpty(bpm_);
    auto fresh_index = std::make_unique<BPlusTree>(bpm_, new_root);
    int64_t row_count = 0;
    t->heap->Scan([&](RID rid, const std::string &data) {
      Tuple tup = Tuple::Deserialize(data, t->schema);
      fresh_index->Insert(tup.Get(pk_idx).AsInt(), rid);
      row_count++;
    });
    t->btree_root = fresh_index->RootPageId();
    t->index = std::move(fresh_index);
    t->num_rows = row_count;
  }
  catalog_->Save();
  bpm_->FlushAllPages();
  disk_manager_->TruncateLog();
  return redo_count;
}

void RecoveryManager::UndoTransaction(txn_id_t id) {
  std::vector<LogRecord> records = ReadAllRecords(disk_manager_);
  std::vector<const LogRecord *> ops;
  std::unordered_set<std::string> touched_tables;
  for (auto &r : records) {
    if (r.txn_id != id) continue;
    if (r.type != LogType::INSERT && r.type != LogType::DELETE) continue;
    ops.push_back(&r);
    touched_tables.insert(r.table);
  }
  for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
    const LogRecord *r = *it;
    Page *page = bpm_->FetchPage(r->rid.page_id);
    HeapPage hp(page->GetData());
    if (r->type == LogType::INSERT) hp.DeleteTuple(r->rid.slot_num);
    else hp.RestoreTuple(r->rid.slot_num, static_cast<int32_t>(r->data.size()));
    bpm_->UnpinPage(r->rid.page_id, true);
  }
  for (auto &name : touched_tables) {
    TableInfo *t = catalog_->GetTable(name);
    if (!t) continue;
    int pk_idx = t->schema.PrimaryKeyIndex();
    if (pk_idx < 0) continue;
    page_id_t new_root = BPlusTree::CreateEmpty(bpm_);
    auto fresh_index = std::make_unique<BPlusTree>(bpm_, new_root);
    int64_t row_count = 0;
    t->heap->Scan([&](RID rid, const std::string &data) {
      Tuple tup = Tuple::Deserialize(data, t->schema);
      fresh_index->Insert(tup.Get(pk_idx).AsInt(), rid);
      row_count++;
    });
    t->btree_root = fresh_index->RootPageId();
    t->index = std::move(fresh_index);
    t->num_rows = row_count;
  }
  catalog_->Save();
}

}  // namespace minidb
