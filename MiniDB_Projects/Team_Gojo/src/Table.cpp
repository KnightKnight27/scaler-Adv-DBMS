#include "Table.h"

#include <cstring>
#include <stdexcept>

// ── Constructors / Destructor ────────────────────────────────────────────

Table::Table(const std::string& name, const std::string& dbFilePath, Schema schema)
    : name_(name), dbFilePath_(dbFilePath), numRows_(0), numDataPages_(0),
      schema_(std::move(schema)) {
  // Compute derived sizing from the schema.
  recordSize_        = schema_.getRecordSize();
  maxRecordsPerPage_ = (Page::PAGE_SIZE - HEADER_SIZE) / recordSize_;

  // Each table gets its own DiskManager (separate .db file on disk)
  // and its own BufferPool for page caching.
  diskManager_ = new DiskManager(dbFilePath);
  bufferPool_  = new BufferPool(diskManager_, 50);

  // The B+ tree index lives in page 0 and potentially additional pages.
  index_ = new BPlusTree(bufferPool_);

  // Allocate the first data page for the heap file (after B+ tree pages).
  allocateDataPage();
}

Table::Table(const std::string& name, const std::string& dbFilePath, Schema schema,
             int rootPageId, int numRows, int numDataPages)
    : name_(name), dbFilePath_(dbFilePath), numRows_(numRows), numDataPages_(0),
      schema_(std::move(schema)) {
  (void)numDataPages;
  recordSize_        = schema_.getRecordSize();
  maxRecordsPerPage_ = (Page::PAGE_SIZE - HEADER_SIZE) / recordSize_;

  diskManager_ = new DiskManager(dbFilePath);
  bufferPool_  = new BufferPool(diskManager_, 50);
  index_       = (rootPageId >= 0) ? new BPlusTree(bufferPool_, rootPageId)
                                  : new BPlusTree(bufferPool_);
  allocateDataPage();
}

Table::~Table() {
  flush();
  delete index_;
  delete bufferPool_;
  delete diskManager_;
}

// ── Record Operations ───────────────────────────────────────────────────

int Table::insertRecord(const Record& r) {
  if (schema_.empty()) {
    throw std::runtime_error("Cannot insert into table without a schema: " + name_);
  }
  if (r.values.size() != schema_.size()) {
    throw std::runtime_error("Insert value count does not match schema for table: " + name_);
  }
  if (!std::holds_alternative<int>(r.values[0])) {
    throw std::runtime_error("Primary key column must be INT for table: " + name_);
  }

  Record stored = r;
  stored._record_id = static_cast<int>(heap_file_.size());
  heap_file_.push_back(stored);

  /*
   * INSERT STRATEGY:
   * Append-only heap file. Map the record id to one of this table's tracked
   * heap pages so B+ tree pages are never mistaken for row-storage pages.
   */
  int dataPageIndex = stored._record_id / maxRecordsPerPage_;
  int slotIdx       = stored._record_id % maxRecordsPerPage_;

  while (dataPageIndex >= static_cast<int>(dataPageIds_.size())) {
    allocateDataPage();
  }

  int dataPageId = dataPageIds_[dataPageIndex];
  Page* page     = bufferPool_->fetchPage(dataPageId);
  int count      = readPageRecordCount(page);

  // Serialize the record into the slot.
  int offset  = slotOffset(slotIdx);
  stored.serialize(page->getData() + offset, schema_);

  if (slotIdx >= count) {
    writePageRecordCount(page, slotIdx + 1);
  }
  page->markDirty();
  bufferPool_->unpinPage(dataPageId, true);

  // Compute the global recordId.
  int recordId = stored._record_id;

  // Index the primary key (first column, must be INT).
  index_->insert(std::get<int>(stored.values[0]), recordId);

  numRows_++;
  return recordId;
}

Record Table::getRecord(int recordId) {
  if (recordId >= 0 && recordId < static_cast<int>(heap_file_.size())) {
    return heap_file_[recordId];
  }

  int pageIndex = recordId / maxRecordsPerPage_;
  int slotIdx   = recordId % maxRecordsPerPage_;
  if (pageIndex < 0 || pageIndex >= static_cast<int>(dataPageIds_.size())) {
    throw std::runtime_error("Record page not found for recordId: " +
                             std::to_string(recordId));
  }
  int pageId = dataPageIds_[pageIndex];

  Page*  page   = bufferPool_->fetchPage(pageId);
  int    offset = slotOffset(slotIdx);
  Record r      = Record::deserialize(page->getData() + offset, schema_);
  bufferPool_->unpinPage(pageId, false);

  return r;
}

void Table::deleteRecord(int recordId) {
  if (recordId >= 0 && recordId < static_cast<int>(heap_file_.size())) {
    if (!heap_file_[recordId].isDeleted()) {
      heap_file_[recordId].markDeleted();
      if (numRows_ > 0) {
        numRows_--;
      }
    }
    return;
  }

  /*
   * DELETION: Tombstone approach — set the deleted_ flag and zero out
   * the slot. The B+ tree entry is left in place (stale) but the scan
   * layer skips deleted records.
   */
  int pageIndex = recordId / maxRecordsPerPage_;
  int slotIdx   = recordId % maxRecordsPerPage_;
  if (pageIndex < 0 || pageIndex >= static_cast<int>(dataPageIds_.size())) {
    throw std::runtime_error("Record page not found for recordId: " +
                             std::to_string(recordId));
  }
  int pageId = dataPageIds_[pageIndex];

  Page* page   = bufferPool_->fetchPage(pageId);
  int   offset = slotOffset(slotIdx);

  Record tombstone;
  tombstone.markDeleted();
  tombstone.serialize(page->getData() + offset, schema_);

  page->markDirty();
  bufferPool_->unpinPage(pageId, true);

  numRows_--;
}

void Table::updateRecord(int recordId, const Record& newRec) {
  Record stored = newRec;
  stored._record_id = recordId;
  if (recordId >= 0 && recordId < static_cast<int>(heap_file_.size())) {
    heap_file_[recordId] = stored;
    return;
  }

  int pageIndex = recordId / maxRecordsPerPage_;
  int slotIdx   = recordId % maxRecordsPerPage_;
  if (pageIndex < 0 || pageIndex >= static_cast<int>(dataPageIds_.size())) {
    throw std::runtime_error("Record page not found for recordId: " +
                             std::to_string(recordId));
  }
  int pageId = dataPageIds_[pageIndex];

  Page* page   = bufferPool_->fetchPage(pageId);
  int   offset = slotOffset(slotIdx);

  stored.serialize(page->getData() + offset, schema_);
  page->markDirty();
  bufferPool_->unpinPage(pageId, true);
}

int Table::searchByKey(int32_t key) { return index_->search(key); }

void Table::flush() { bufferPool_->flushAllPages(); }

void Table::loadPersistedRecord(const Record& r) {
  Record stored = r;
  if (stored._record_id < 0) {
    stored._record_id = static_cast<int>(heap_file_.size());
  }
  if (stored._record_id >= static_cast<int>(heap_file_.size())) {
    heap_file_.resize(stored._record_id + 1);
  }
  heap_file_[stored._record_id] = stored;
  if (!stored.isDeleted()) {
    numRows_++;
  }
}

void Table::rebuildIndex() {
  delete index_;
  index_ = new BPlusTree(bufferPool_);
  for (const auto& r : heap_file_) {
    if (!r.isDeleted() && !r.values.empty() && std::holds_alternative<int>(r.values[0])) {
      index_->insert(std::get<int>(r.values[0]), r._record_id);
    }
  }
}

// ── Internal helpers ────────────────────────────────────────────────────

int Table::readPageRecordCount(Page* page) {
  int32_t count;
  std::memcpy(&count, page->getData(), sizeof(int32_t));
  return count;
}

void Table::writePageRecordCount(Page* page, int count) {
  int32_t c = count;
  std::memcpy(page->getData(), &c, sizeof(int32_t));
}

int Table::slotOffset(int slotIndex) const {
  return HEADER_SIZE + slotIndex * recordSize_;
}

int Table::allocateDataPage() {
  int pageId = diskManager_->allocatePage();
  dataPageIds_.push_back(pageId);
  numDataPages_ = static_cast<int>(dataPageIds_.size());

  Page* page = bufferPool_->fetchPage(pageId);
  writePageRecordCount(page, 0);
  bufferPool_->unpinPage(pageId, true);

  return pageId;
}
