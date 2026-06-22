#pragma once
#include <memory>
#include <string>
#include "common/types.h"
#include "storage/table_heap.h"
#include "index/bplus_tree.h"
#include "lsm/lsm_tree.h"

namespace minidb {

// A forward cursor over serialized row bytes (used to stream scans into the
// executor without exposing the underlying engine's iteration mechanism).
class RowCursor {
 public:
  virtual ~RowCursor() = default;
  virtual bool Next(std::string* row) = 0;  // false at end
};

// The storage abstraction the rest of the engine talks to. Two engines
// implement it, so the SQL executor / optimizer / transaction layer run
// unchanged over either: the B+ tree row store (heap pages + PK index) and the
// LSM-tree store (Track C). Rows are addressed by an int64 primary key.
class RowStore {
 public:
  virtual ~RowStore() = default;

  virtual std::unique_ptr<RowCursor> FullScan() = 0;
  virtual std::unique_ptr<RowCursor> RangeScan(int64_t low, int64_t high) = 0;
  virtual bool Point(int64_t key, std::string* row) = 0;
  virtual void Insert(int64_t key, const std::string& row) = 0;
  virtual void Delete(int64_t key) = 0;

  // True if point/range lookups are key-accelerated (PK index or sorted runs),
  // so the optimizer may consider an index/range scan.
  virtual bool SupportsKeyAccess() const = 0;

  // Make in-memory state durable at a checkpoint (LSM flushes its memtable;
  // the heap engine is a no-op since it persists via the buffer pool).
  virtual void Sync() {}
};

// ---- B+ tree row store: rows in a heap, primary key -> RID in a B+ tree ----
class HeapRowStore : public RowStore {
 public:
  HeapRowStore(std::shared_ptr<TableHeap> heap, std::shared_ptr<BPlusTree> index)
      : heap_(std::move(heap)), index_(std::move(index)) {}

  std::unique_ptr<RowCursor> FullScan() override;
  std::unique_ptr<RowCursor> RangeScan(int64_t low, int64_t high) override;
  bool Point(int64_t key, std::string* row) override;
  void Insert(int64_t key, const std::string& row) override;
  void Delete(int64_t key) override;
  bool SupportsKeyAccess() const override { return index_ != nullptr; }

 private:
  std::shared_ptr<TableHeap> heap_;
  std::shared_ptr<BPlusTree> index_;  // may be null (no usable PK index)
};

// ---- LSM-tree row store (Extension Track C) ----
class LsmRowStore : public RowStore {
 public:
  explicit LsmRowStore(std::shared_ptr<LSMTree> lsm) : lsm_(std::move(lsm)) {}

  std::unique_ptr<RowCursor> FullScan() override;
  std::unique_ptr<RowCursor> RangeScan(int64_t low, int64_t high) override;
  bool Point(int64_t key, std::string* row) override;
  void Insert(int64_t key, const std::string& row) override;
  void Delete(int64_t key) override;
  bool SupportsKeyAccess() const override { return true; }
  void Sync() override;

  LSMTree* Tree() { return lsm_.get(); }

 private:
  std::shared_ptr<LSMTree> lsm_;
};

}  // namespace minidb
