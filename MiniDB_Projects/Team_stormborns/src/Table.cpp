#include "Table.h"

#include <cstring>
#include <stdexcept>

// ── Constructor / Destructor ────────────────────────────────────────────

Table::Table(const std::string& name, const std::string& dbFilePath, Schema schema)
    : name_(name), numRows_(0), numDataPages_(0), schema_(std::move(schema))
{
    recordSize_ = schema_.getRecordSize();
    maxRecordsPerPage_ = (Page::PAGE_SIZE - HEADER_SIZE) / recordSize_;

    // Each table gets its own DiskManager (separate .db file on disk)
    // and its own BufferPool for page caching.
    diskManager_ = new DiskManager(dbFilePath);
    bufferPool_  = new BufferPool(diskManager_, 50);

    // The B+ tree index lives in a SEPARATE file from the heap data.
    // This mirrors how real databases (e.g., InnoDB secondary indexes)
    // keep index data separate from table data.
    index_ = new BPlusTree(bufferPool_);

    // Allocate the first data page for the heap file.
    // NOTE: The BPlusTree constructor already allocated page 0 for its root.
    // Our data pages start after whatever the B+ tree has allocated.
    int firstDataPage = diskManager_->allocatePage();
    numDataPages_ = 1;

    // Initialize the first data page with zero records
    Page* page = bufferPool_->fetchPage(firstDataPage);
    writePageRecordCount(page, 0);
    bufferPool_->unpinPage(firstDataPage, true);
}

Table::Table(const std::string& name, const std::string& dbFilePath, Schema schema,
             int rootPageId, int numRows, int numDataPages)
    : name_(name), numRows_(numRows), numDataPages_(numDataPages), schema_(std::move(schema))
{
    recordSize_ = schema_.getRecordSize();
    maxRecordsPerPage_ = (Page::PAGE_SIZE - HEADER_SIZE) / recordSize_;

    diskManager_ = new DiskManager(dbFilePath);
    bufferPool_  = new BufferPool(diskManager_, 50);

    // Reconnect the existing index using saved rootPageId
    index_ = new BPlusTree(bufferPool_, rootPageId);
}

Table::~Table() {
    flush();
    delete index_;
    delete bufferPool_;
    delete diskManager_;
}

// ── Record Operations ───────────────────────────────────────────────────

int Table::insertRecord(const Record& r) {
    int lastDataPageId = diskManager_->getNumPages() - 1;
    
    // Try to find space in the last page
    Page* page = bufferPool_->fetchPage(lastDataPageId);
    int count = readPageRecordCount(page);

    if (count >= maxRecordsPerPage_) {
        // Last page is full — allocate a new data page
        bufferPool_->unpinPage(lastDataPageId, false);

        lastDataPageId = diskManager_->allocatePage();
        numDataPages_++;

        page = bufferPool_->fetchPage(lastDataPageId);
        count = 0;
        writePageRecordCount(page, 0);
    }

    // Write the record into the next available slot
    int slotIdx = count;
    int offset = slotOffset(slotIdx);
    r.serialize(page->getData() + offset, schema_);

    // Update the record count in the page header
    writePageRecordCount(page, count + 1);
    page->markDirty();
    bufferPool_->unpinPage(lastDataPageId, true);

    // Compute the global recordId
    // We use a flat encoding: recordId = pageId * maxRecordsPerPage_ + slotIdx
    int recordId = lastDataPageId * maxRecordsPerPage_ + slotIdx;

    // Insert into the B+ tree index (key = first column (PK), value = recordId)
    int32_t pk = std::get<int32_t>(r.values[0]);
    index_->insert(pk, recordId);

    numRows_++;
    return recordId;
}

Record Table::getRecord(int recordId) {
    int pageId   = recordId / maxRecordsPerPage_;
    int slotIdx  = recordId % maxRecordsPerPage_;

    Page* page = bufferPool_->fetchPage(pageId);
    int offset = slotOffset(slotIdx);
    Record r = Record::deserialize(page->getData() + offset, schema_);
    bufferPool_->unpinPage(pageId, false);

    return r;
}

void Table::deleteRecord(int recordId) {
    int pageId  = recordId / maxRecordsPerPage_;
    int slotIdx = recordId % maxRecordsPerPage_;

    Page* page = bufferPool_->fetchPage(pageId);
    int offset = slotOffset(slotIdx);

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

    Page* page = bufferPool_->fetchPage(pageId);
    int offset = slotOffset(slotIdx);

    newRec.serialize(page->getData() + offset, schema_);
    page->markDirty();
    bufferPool_->unpinPage(pageId, true);
}

int Table::searchByKey(int32_t key) {
    return index_->search(key);
}

void Table::flush() {
    bufferPool_->flushAllPages();
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
