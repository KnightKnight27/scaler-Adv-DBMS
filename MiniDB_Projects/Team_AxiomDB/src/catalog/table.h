#pragma once

#include <memory>
#include <optional>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "catalog/tuple.h"
#include "common/rid.h"
#include "common/status.h"
#include "index/bplus_tree_index.h"
#include "storage/heap_table.h"

namespace axiomdb {

// ---------------------------------------------------------------------------
// Table -- the runtime accessor that ties a table's heap file and primary-key
// B+tree together and exposes relational row operations.  This is the bridge
// the executor sits on: SeqScan walks heap(), IndexScan probes index() then
// fetches from the heap, INSERT/DELETE keep the heap and index in step.
//
// A row's identity is its primary key (if declared); the index maps the
// order-preserving encoding of that key value to the row's RID in the heap.
// ---------------------------------------------------------------------------
class Table {
 public:
  Table(BufferPoolManager* bpm, TableInfo* info);

  TableInfo* info() const { return info_; }
  const Schema& schema() const { return info_->schema; }
  bool has_index() const { return index_ != nullptr; }

  // Insert a row.  On a primary-key table, enforces uniqueness (returns
  // AlreadyExists on a duplicate key).  Writes the new RID to *out_rid.
  Status insert(const Tuple& tuple, RID* out_rid = nullptr);

  // Delete the row at `rid`.  Reads it first to remove the matching index
  // entry.  Returns true if the row was present.
  bool erase(RID rid);

  std::optional<Tuple> get(RID rid) const;

  // Primary-key point lookup (the IndexScan fast path).  nullopt if there is no
  // PK index or the key is absent.
  std::optional<RID> lookup_pk(const Value& key) const;

  // Idempotent variants used by crash recovery's logical replay:
  //   upsert       -- insert, or replace the existing row with the same PK
  //   delete_by_pk -- delete the row with this PK if present (no-op otherwise)
  Status upsert(const Tuple& tuple, RID* out_rid = nullptr);
  bool delete_by_pk(const Value& key);

  HeapTable* heap() { return heap_.get(); }
  BPlusTreeIndex* index() { return index_.get(); }  // nullptr if the table has no PK

 private:
  BufferPoolManager* bpm_;
  TableInfo* info_;
  std::unique_ptr<HeapTable> heap_;
  std::unique_ptr<BPlusTreeIndex> index_;  // primary-key index, nullptr if none
};

}  // namespace axiomdb
