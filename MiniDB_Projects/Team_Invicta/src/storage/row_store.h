#pragma once
#include <cstdint>
#include <memory>
#include "record/schema.h"
#include "record/tuple.h"

namespace minidb {

// Streaming cursor over rows (Volcano-friendly): Next() yields one row at a
// time and returns false when exhausted.
class RowCursor {
 public:
  virtual ~RowCursor() = default;
  virtual bool Next(Tuple *out) = 0;
};

// Abstract primary-key-addressed row store. This is the single seam that lets
// the same SQL engine run over either the B+ tree heap store or the LSM store.
// All access is by the table's int64 primary key.
class RowStore {
 public:
  virtual ~RowStore() = default;

  virtual void Insert(int64_t key, const Tuple &row) = 0;
  virtual bool Get(int64_t key, Tuple *out) = 0;
  virtual bool Delete(int64_t key) = 0;

  // Full scan in unspecified order (heap order / merged LSM order).
  virtual std::unique_ptr<RowCursor> ScanAll() = 0;

  // Inclusive primary-key range scan [low, high], in key order.
  virtual std::unique_ptr<RowCursor> RangeScan(int64_t low, int64_t high) = 0;

  // Approximate live row count (used by the optimizer for statistics).
  virtual size_t RowCount() = 0;

  // Force any in-memory state durable (e.g. flush an LSM MemTable). Heap stores
  // rely on the buffer pool flush and leave this a no-op.
  virtual void Sync() {}

  // Observed [min,max] primary key. Returns false if the store is empty.
  // Used by the optimizer to estimate range selectivity.
  virtual bool KeyRange(int64_t *min_key, int64_t *max_key) = 0;

  virtual const Schema &schema() const = 0;
};

}  // namespace minidb
