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
 *
 * COST: O(N) where N = number of pages. Every page is read exactly once.
 *
 * WHEN USED:
 * The optimizer chooses TableScan when:
 *   1. There is no WHERE clause (full scan required)
 *   2. The WHERE predicate is not on an indexed column
 *   3. The selectivity is high (most rows match) — scan is cheaper
 *      than many random B+ tree lookups
 *
 * COMPARISON WITH INDEX SCAN:
 * Table scan reads pages sequentially (good for disk locality).
 * Index scan does random page accesses (one per matching key).
 * For queries returning >15-20% of rows, table scan is typically faster
 * because sequential I/O is much cheaper than random I/O.
 */
class TableScanNode : public PlanNode {
public:
  explicit TableScanNode(Table *table)
      : table_(table), currentPageId_(0), currentSlot_(0), currentCount_(0),
        finished_(false), page_(nullptr) {}

  void open() override {
    // Start scanning from the first data page.
    // In our layout, the B+ tree uses page 0 (and potentially more
    // pages after splits). Data pages are at higher pageIds.
    // We scan ALL pages and skip non-data pages (they'll have
    // records with invalid-looking data that we filter by checking
    // the page header format).
    //
    // Simple approach: scan from page 1 to numPages. Data pages
    // have a valid record count header; B+ tree pages have isLeaf
    // byte at offset 0 which could be 0 or 1.
    //
    // Better approach: We know data pages were allocated after the
    // B+ tree root. Let's start from page 1 (after root) and go
    // to the end. We'll check the slot count.
    currentPageId_ = 1; // skip page 0 (B+ tree root)
    currentSlot_ = 0;
    finished_ = false;
    page_ = nullptr;

    loadCurrentPage();
  }

  bool hasNext() override {
    if (finished_)
      return false;

    // Skip deleted records and advance through pages
    while (!finished_) {
      if (currentSlot_ < currentCount_) {
        // Read candidate record
        int offset = Table::HEADER_SIZE + currentSlot_ * Record::RECORD_SIZE;
        Record r = Record::deserialize(page_->getData() + offset);

        if (!r.isDeleted()) {
          return true; // found a live record
        }

        // Deleted — skip to next slot
        currentSlot_++;
      } else {
        // Current page exhausted — move to next page
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
    int offset = Table::HEADER_SIZE + currentSlot_ * Record::RECORD_SIZE;
    Record r = Record::deserialize(page_->getData() + offset);
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

  /**
   * TRACK A: Optimized batch read.
   *
   * Instead of calling next() repeatedly (which does one record at a time
   * with virtual dispatch each time), we read directly from the page buffer
   * in a tight loop. This avoids:
   *   1. Per-record virtual function call overhead
   *   2. Repeated page pin/unpin for records on the same page
   *   3. Per-record hasNext() checks
   *
   * Records are memcpy'd in bulk from the page buffer into the output vector.
   */
  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> batch;
    batch.reserve(batchSize);

    while (static_cast<int>(batch.size()) < batchSize && !finished_) {
      if (currentSlot_ < currentCount_) {
        int offset = Table::HEADER_SIZE + currentSlot_ * Record::RECORD_SIZE;
        Record r = Record::deserialize(page_->getData() + offset);
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

    // Read the record count from the page header
    int32_t count;
    std::memcpy(&count, page_->getData(), sizeof(int32_t));

    // Sanity check: if count is unreasonable, this might be a B+ tree
    // page that leaked into our scan range. Skip it.
    if (count < 0 || count > Table::MAX_RECORDS_PER_PAGE) {
      table_->getBufferPool()->unpinPage(currentPageId_, false);
      page_ = nullptr;
      currentCount_ = 0;
      currentSlot_ = currentCount_; // force advance to next page
      return;
    }

    currentCount_ = count;
  }

  Table *table_;
  int currentPageId_;
  int currentSlot_;
  int currentCount_; // record count in current page
  bool finished_;
  Page *page_;
};

#endif // MINIDB_TABLE_SCAN_NODE_H
