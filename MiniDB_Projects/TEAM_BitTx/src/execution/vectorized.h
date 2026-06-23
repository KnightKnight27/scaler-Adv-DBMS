#pragma once

#include "common/tuple.h"
#include "common/types.h"
#include "execution/executor.h"

#include <cstddef>
#include <vector>

#include "storage/columnar.h"
#include <memory>

namespace minidb {

// Batch of tuples scanned together. Columnar-friendly: callers can iterate
// rows or pull per-column values via Row().
class ColumnBatch {
public:
  void Clear();
  void Push(const Tuple& t);
  size_t Size() const;
  const Tuple& Row(size_t idx) const;

private:
  std::vector<Tuple> rows_;
};

// Vectorized sequential scan: reads tuples from a TableHeap in batches.
class VectorizedSeqScanExecutor : public AbstractExecutor {
public:
  VectorizedSeqScanExecutor(TableHeap* table, size_t batchSize);
  ~VectorizedSeqScanExecutor() override = default;

  void Init() override;
  size_t NextBatch(ColumnBatch* out);
  const Schema& GetOutputSchema() const override;
  bool Next(Tuple* out) override;

private:
  TableHeap* table_;
  size_t batchSize_;
  size_t cursor_;
  std::unique_ptr<ColumnarFile> colFile_;
  std::vector<Tuple> colRows_;
};

} // namespace minidb