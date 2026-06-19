#pragma once

#include <cstdint>

#include "buffer/buffer_pool_manager.h"
#include "catalog/schema.h"
#include "storage/table_heap.h"

namespace minidb {

// Track A (Performance): a minimal vectorized, batch-at-a-time execution path for
// the common integer scan / filter / aggregate case, plus a row-at-a-time
// baseline to benchmark against.
//
// The vectorized path targets all-INTEGER tables: an INTEGER column lives at a
// fixed byte offset (4 * column index) in the tuple. It walks the heap one PAGE
// at a time (fetching each page from the buffer pool exactly once), decodes the
// needed columns straight from the page buffer into contiguous int32 arrays, and
// filters + aggregates each page's batch in a tight loop.
//
// The baseline uses the tuple-at-a-time heap iterator and materializes a Value
// vector per tuple — exactly what the Volcano executors do. The difference the
// benchmark isolates: amortized page access + no per-tuple Value allocation.
class VectorizedEngine {
 public:
  VectorizedEngine(TableHeap *heap, BufferPoolManager *bpm, const Schema *schema)
      : heap_(heap), bpm_(bpm), schema_(schema), first_page_(heap->GetFirstPageId()) {}

  // Vectorized SUM(sum_col) over rows where filter_col < threshold.
  long FilterSum(int filter_col, int32_t threshold, int sum_col) const;

  // Row-at-a-time equivalent (materializes a Value vector per tuple) — baseline.
  long RowAtATimeFilterSum(int filter_col, int32_t threshold, int sum_col) const;

 private:
  TableHeap *heap_;
  BufferPoolManager *bpm_;
  const Schema *schema_;
  page_id_t first_page_;
};

}  // namespace minidb
