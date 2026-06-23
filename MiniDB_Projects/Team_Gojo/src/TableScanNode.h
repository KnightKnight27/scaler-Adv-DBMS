#ifndef MINIDB_TABLE_SCAN_NODE_H
#define MINIDB_TABLE_SCAN_NODE_H

#include <cstring>

#include "PlanNode.h"
#include "Table.h"

/**
 * TableScanNode — Sequential scan of all records in a heap file.
 *
 * This is the simplest and most expensive access method. It reads
 * every page in the table and returns every non-deleted record.
 */
class TableScanNode : public PlanNode {
public:
  explicit TableScanNode(Table *table)
      : table_(table), currentRecord_(0), finished_(false) {}

  const Schema& getSchema() const override {
    return table_->getSchema();
  }

  void open() override {
    currentRecord_ = 0;
    finished_      = false;
  }

  bool hasNext() override {
    if (finished_) return false;

    const auto& heap = table_->getHeapFile();
    while (currentRecord_ < heap.size()) {
      if (!heap[currentRecord_].isDeleted()) {
        return true;
      }
      currentRecord_++;
    }
    finished_ = true;
    return false;
  }

  Record next() override {
    Record r = table_->getHeapFile()[currentRecord_];
    currentRecord_++;
    return r;
  }

  void close() override {
    finished_ = true;
  }

  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> batch;
    batch.reserve(batchSize);

    while (static_cast<int>(batch.size()) < batchSize && hasNext()) {
      batch.push_back(next());
    }
    return batch;
  }

private:
  Table *table_;
  size_t currentRecord_;
  bool finished_;
};

#endif // MINIDB_TABLE_SCAN_NODE_H
