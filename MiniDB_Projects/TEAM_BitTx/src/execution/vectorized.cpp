#include "execution/vectorized.h"

#include "catalog/catalog.h"
#include "catalog/table_heap.h"
#include "common/rid.h"
#include "common/tuple.h"
#include "execution/executor.h"
#include "storage/heap_file.h"

namespace minidb {

void ColumnBatch::Clear() {
  rows_.clear();
}

void ColumnBatch::Push(const Tuple& t) {
  rows_.push_back(t);
}

size_t ColumnBatch::Size() const {
  return rows_.size();
}

const Tuple& ColumnBatch::Row(size_t idx) const {
  return rows_[idx];
}

VectorizedSeqScanExecutor::VectorizedSeqScanExecutor(TableHeap* table, size_t batchSize)
    : table_(table), batchSize_(batchSize), cursor_(0) {}

void VectorizedSeqScanExecutor::Init() {
  cursor_ = 0;
  colRows_.clear();
  if (table_ == nullptr)
    return;

  // 1. Create a columnar representation of the table heap data
  string colPath = "/tmp/minidb_vec_" + to_string((uintptr_t)table_) + ".col";
  remove(colPath.c_str());

  colFile_ = make_unique<ColumnarFile>(colPath, table_->GetSchema());

  // Load data from row-store into the columnar store to simulate a hybrid / dual-format engine
  HeapFile* hf = table_->GetHeap();
  if (hf) {
    for (auto it = hf->Begin(); it != hf->End(); ++it) {
      Tuple t;
      if (table_->GetTuple(it.GetRid(), &t)) {
        colFile_->AppendRow(t);
      }
    }
  }

  // 2. Scan directly from the ColumnarFile instead of iterating row-by-row on TableHeap
  colRows_ = colFile_->Scan();

  // Clean up the temporary columnar file
  remove(colPath.c_str());
}

size_t VectorizedSeqScanExecutor::NextBatch(ColumnBatch* out) {
  out->Clear();
  size_t produced = 0;
  while (cursor_ < colRows_.size() && produced < batchSize_) {
    out->Push(colRows_[cursor_++]);
    ++produced;
  }
  return produced;
}

const Schema& VectorizedSeqScanExecutor::GetOutputSchema() const {
  return table_->GetSchema();
}

bool VectorizedSeqScanExecutor::Next(Tuple* out) {
  ColumnBatch tmp;
  if (NextBatch(&tmp) == 0)
    return false;
  *out = tmp.Row(0);
  return true;
}

} // namespace minidb