#ifndef MINIDB_INDEX_SCAN_NODE_H
#define MINIDB_INDEX_SCAN_NODE_H

#include "PlanNode.h"
#include "Table.h"

/**
 * IndexScanNode — Point lookup using the B+ tree index.
 *
 * Uses the B+ tree to find a single record by its primary key.
 * This is O(log N) in the number of keys, compared to O(N) for a
 * full table scan.
 */
class IndexScanNode : public PlanNode {
public:
  IndexScanNode(Table *table, int32_t searchKey)
      : table_(table), searchKey_(searchKey), consumed_(false) {}

  const Schema& getSchema() const override {
    return table_->getSchema();
  }

  void open() override { consumed_ = false; }

  bool hasNext() override { return !consumed_; }

  Record next() override {
    consumed_ = true;

    int recordId = table_->searchByKey(searchKey_);

    if (recordId == -1) {
      // Key not found — return a "deleted" marker record.
      Record r;
      r.markDeleted();
      return r;
    }

    return table_->getRecord(recordId);
  }

  void close() override { consumed_ = true; }

  std::vector<Record> nextBatch(int batchSize = 100) override {
    return PlanNode::nextBatch(batchSize);
  }

private:
  Table *table_;
  int32_t searchKey_;
  bool consumed_;
};

#endif // MINIDB_INDEX_SCAN_NODE_H
