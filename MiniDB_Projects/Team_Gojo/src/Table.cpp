#include "Table.h"

#include <cstring>

// ── Constructors / Destructor ────────────────────────────────────────────

Table::Table(const std::string& name, const std::string& dbFilePath, Schema schema)
    : name_(name), numRows_(0), numDataPages_(0), schema_(std::move(schema)) {
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
  int firstDataPage = diskManager_->allocatePage();
  numDataPages_ = 1;

  // Initialize the first data page with zero records.
  Page* page = bufferPool_->fetchPage(firstDataPage);
  writePageRecordCount(page, 0);
  bufferPool_->unpinPage(firstDataPage, true);
}

Table::Table(const std::string& name, const std::string& dbFilePath, Schema schema,
             int /*rootPageId*/, int numRows, int numDataPages)
    : name_(name), numRows_(numRows), numDataPages_(numDataPages),
      schema_(std::move(schema)) {
  recordSize_        = schema_.getRecordSize();
  maxRecordsPerPage_ = (Page::PAGE_SIZE - HEADER_SIZE) / recordSize_;

  diskManager_ = new DiskManager(dbFilePath);
  bufferPool_  = new BufferPool(diskManager_, 50);
  index_       = new BPlusTree(bufferPool_);
}

Table::~Table() {
  flush();
  delete index_;
  delete bufferPool_;
  delete diskManager_;
}

// ── Record Operations ───────────────────────────────────────────────────

int Table::insertRecord(const Record& r) {
  /*
   * INSERT STRATEGY:
   * Append-only heap file. Check the last data page; if full, allocate
   * a new page. The B+ tree index on the first (primary-key) column is
   * updated after each insert.
   */
  int lastDataPageId = diskManager_->getNumPages() - 1;

  Page* page  = bufferPool_->fetchPage(lastDataPageId);
  int   count = readPageRecordCount(page);

  if (count >= maxRecordsPerPage_) {
    // Last page is full — allocate a new data page.
    bufferPool_->unpinPage(lastDataPageId, false);

    lastDataPageId = diskManager_->allocatePage();
    numDataPages_++;

    page  = bufferPool_->fetchPage(lastDataPageId);
    count = 0;
    writePageRecordCount(page, 0);
  }

  // Serialize the record into the slot.
  int slotIdx = count;
  int offset  = slotOffset(slotIdx);
  r.serialize(page->getData() + offset, schema_);

  writePageRecordCount(page, count + 1);
  page->markDirty();
  bufferPool_->unpinPage(lastDataPageId, true);

  // Compute the global recordId.
  int recordId = lastDataPageId * maxRecordsPerPage_ + slotIdx;

  // Index the primary key (first column, must be INT).
  if (!r.values.empty() && std::holds_alternative<int32_t>(r.values[0])) {
    index_->insert(std::get<int32_t>(r.values[0]), recordId);
  }

  numRows_++;
  return recordId;
}

Record Table::getRecord(int recordId) {
  int pageId  = recordId / maxRecordsPerPage_;
  int slotIdx = recordId % maxRecordsPerPage_;

  Page*  page   = bufferPool_->fetchPage(pageId);
  int    offset = slotOffset(slotIdx);
  Record r      = Record::deserialize(page->getData() + offset, schema_);
  bufferPool_->unpinPage(pageId, false);

  return r;
}

void Table::deleteRecord(int recordId) {
  /*
   * DELETION: Tombstone approach — set the deleted_ flag and zero out
   * the slot. The B+ tree entry is left in place (stale) but the scan
   * layer skips deleted records.
   */
  int pageId  = recordId / maxRecordsPerPage_;
  int slotIdx = recordId % maxRecordsPerPage_;

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
  int pageId  = recordId / maxRecordsPerPage_;
  int slotIdx = recordId % maxRecordsPerPage_;

  Page* page   = bufferPool_->fetchPage(pageId);
  int   offset = slotOffset(slotIdx);

  newRec.serialize(page->getData() + offset, schema_);
  page->markDirty();
  bufferPool_->unpinPage(pageId, true);
}

int Table::searchByKey(int32_t key) { return index_->search(key); }

void Table::flush() { bufferPool_->flushAllPages(); }

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
