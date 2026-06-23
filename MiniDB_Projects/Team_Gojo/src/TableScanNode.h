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
      : table_(table), currentPageId_(0), currentSlot_(0), currentCount_(0),
        finished_(false), page_(nullptr) {}

  const Schema& getSchema() const override {
    return table_->getSchema();
  }

  void open() override {
    currentPageId_ = 1;  // skip page 0 (B+ tree root)
    currentSlot_   = 0;
    finished_      = false;
    page_          = nullptr;
    loadCurrentPage();
  }

  bool hasNext() override {
    if (finished_) return false;

    while (!finished_) {
      if (currentSlot_ < currentCount_) {
        int offset = Table::HEADER_SIZE + currentSlot_ * table_->getRecordSize();
        Record r = Record::deserialize(page_->getData() + offset, table_->getSchema());

        if (!r.isDeleted()) {
          return true;
        }
        currentSlot_++;
      } else {
        if (page_) {
          table_->getBufferPool()->unpinPage(currentPageId_, false);
          page_ = nullptr;
        }
        currentPageId_++;
        currentSlot_ = 0;
        loadCurrentPage();
      }
    }
    return false;
  }

  Record next() override {
    int offset = Table::HEADER_SIZE + currentSlot_ * table_->getRecordSize();
    Record r   = Record::deserialize(page_->getData() + offset, table_->getSchema());
    currentSlot_++;
    return r;
  }

  void close() override {
    if (page_) {
      table_->getBufferPool()->unpinPage(currentPageId_, false);
      page_ = nullptr;
    }
    finished_ = true;
  }

  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> batch;
    batch.reserve(batchSize);

    while (static_cast<int>(batch.size()) < batchSize && !finished_) {
      if (currentSlot_ < currentCount_) {
        int offset = Table::HEADER_SIZE + currentSlot_ * table_->getRecordSize();
        Record r   = Record::deserialize(page_->getData() + offset, table_->getSchema());
        currentSlot_++;
        if (!r.isDeleted()) {
          batch.push_back(r);
        }
      } else {
        if (page_) {
          table_->getBufferPool()->unpinPage(currentPageId_, false);
          page_ = nullptr;
        }
        currentPageId_++;
        currentSlot_ = 0;
        loadCurrentPage();
      }
    }
    return batch;
  }

private:
  void loadCurrentPage() {
    int totalPages = table_->getDiskManager()->getNumPages();
    if (currentPageId_ >= totalPages) {
      finished_ = true;
      return;
    }

    page_ = table_->getBufferPool()->fetchPage(currentPageId_);

    int32_t count;
    std::memcpy(&count, page_->getData(), sizeof(int32_t));

    // Sanity-check: if count is unreasonable this is a B+ tree page; skip it.
    if (count < 0 || count > table_->getMaxRecordsPerPage()) {
      table_->getBufferPool()->unpinPage(currentPageId_, false);
      page_         = nullptr;
      currentCount_ = 0;
      currentPageId_++;
      loadCurrentPage();
      return;
    }
    currentCount_ = count;
  }

  Table *table_;
  int currentPageId_;
  int currentSlot_;
  int currentCount_;
  bool finished_;
  Page *page_;
};

#endif // MINIDB_TABLE_SCAN_NODE_H
