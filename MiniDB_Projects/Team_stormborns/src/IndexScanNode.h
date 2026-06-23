#ifndef MINIDB_INDEX_SCAN_NODE_H
#define MINIDB_INDEX_SCAN_NODE_H

#include "PlanNode.h"
#include "Table.h"

/**
 * IndexScanNode — Point lookup using the B+ tree index.
 *
 * Uses the B+ tree to find a single record by its primary key (id).
 * This is O(log N) in the number of keys, compared to O(N) for a
 * full table scan.
 *
 * WHEN USED:
 * The optimizer chooses IndexScan when:
 *   1. The WHERE clause has an equality predicate on the indexed column (id)
 *   2. The selectivity is very low (only 1 row expected to match)
 *
 * COST MODEL:
 *   Cost = height of B+ tree ≈ log_510(N) page reads
 *   For 1M records: log_510(1,000,000) ≈ 2.2 → only ~3 page reads!
 *   Compare with table scan: ~2000 page reads for 1M records.
 *
 * LIMITATION:
 * Our current implementation only supports exact equality lookups
 * on the primary key. A production database would also support range
 * scans (BETWEEN, <, >) by scanning leaf pages left-to-right using
 * sibling pointers. We don't have sibling pointers in our B+ tree
 * implementation (a known simplification).
 */
class IndexScanNode : public PlanNode {
public:
  IndexScanNode(Table *table, int32_t searchKey)
      : table_(table), searchKey_(searchKey), consumed_(false) {}

  void open() override { consumed_ = false; }

  bool hasNext() override { return !consumed_; }

  Record next() override {
    consumed_ = true;

    // Use the B+ tree to find the recordId for this key
    int recordId = table_->searchByKey(searchKey_);

    if (recordId == -1) {
      // Key not found — return a "deleted" marker record
      Record r;
      r.markDeleted();
      return r;
    }

    // Fetch the actual record from the heap file
    return table_->getRecord(recordId);
  }

  void close() override { consumed_ = true; }

private:
  Table *table_;
  int32_t searchKey_;
  bool consumed_;
};

#endif // MINIDB_INDEX_SCAN_NODE_H
