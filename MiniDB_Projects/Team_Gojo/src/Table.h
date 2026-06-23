#ifndef MINIDB_TABLE_H
#define MINIDB_TABLE_H

#include <string>
#include <vector>

#include "BPlusTree.h"
#include "BufferPool.h"
#include "DiskManager.h"
#include "Record.h"
#include "Schema.h"

/**
 * Table implements a heap-file storage structure for fixed-schema records.
 *
 * ARCHITECTURE:
 * Each Table owns its own DiskManager (separate .db file), BufferPool,
 * and BPlusTree index on the primary key (first column).
 */
class Table {
public:
    static constexpr int HEADER_SIZE = 4;  // 4 bytes for numRecords count

    /**
     * Creates a new table backed by the given file.
     * Opens (or creates) the database file and initializes the B+ tree index.
     */
    Table(const std::string& name, const std::string& dbFilePath,
          Schema schema = Schema({{"id", DataType::INT, 4}, {"val", DataType::INT, 4}}));

    /**
     * Restoring constructor for loading a table from dynamic catalog.
     */
    Table(const std::string& name, const std::string& dbFilePath, Schema schema,
          int rootPageId, int numRows, int numDataPages);

    ~Table();

    // Non-copyable
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    /** Insert a record and index it by primary key. Returns the recordId. */
    int insertRecord(const Record& r);

    /** Retrieve a record by its encoded recordId (pageId * MAX + slot). */
    Record getRecord(int recordId);

    /** Delete a record by recordId (tombstone). */
    void deleteRecord(int recordId);

    /** Overwrite a record in-place at the given recordId. */
    void updateRecord(int recordId, const Record& newRec);

    /** Search for a record by primary key using the B+ tree index. */
    int searchByKey(int32_t key);

    /** Get the total number of live (non-deleted) records. */
    int getNumRows() const { return numRows_; }

    /** Get the table name. */
    const std::string& getName() const { return name_; }

    /** Flush all dirty pages to disk. */
    void flush();

    // ── Accessors for the execution engine ──────────────────────────────

    BufferPool* getBufferPool() { return bufferPool_; }
    DiskManager* getDiskManager() { return diskManager_; }
    BPlusTree* getIndex() { return index_; }
    const BPlusTree* getIndex() const { return index_; }

    /** Total number of data pages allocated for this table. */
    int getNumDataPages() const { return numDataPages_; }

    const Schema& getSchema() const { return schema_; }
    int getRecordSize() const { return recordSize_; }
    int getMaxRecordsPerPage() const { return maxRecordsPerPage_; }
    const std::string& getDbFilePath() const { return dbFilePath_; }
    const std::vector<Record>& getHeapFile() const { return heap_file_; }
    std::vector<Record>& getHeapFile() { return heap_file_; }

    /** Load a persisted heap record without allocating a new id or reindexing. */
    void loadPersistedRecord(const Record& r);

    /** Rebuild the in-memory/table B+ tree index from the heap vector. */
    void rebuildIndex();

private:
    /** Read the numRecords count from a page's header. */
    int readPageRecordCount(Page* page);

    /** Write the numRecords count to a page's header. */
    void writePageRecordCount(Page* page, int count);

    /** Compute the byte offset of a record slot within a page. */
    int slotOffset(int slotIndex) const;

    /** Allocate a clean heap data page and remember its page id. */
    int allocateDataPage();

    std::string name_;
    std::string dbFilePath_;
    DiskManager* diskManager_;
    BufferPool* bufferPool_;
    BPlusTree* index_;
    int numRows_;
    int numDataPages_;  // tracks how many pages we've allocated for records

    Schema schema_;
    int recordSize_;
    int maxRecordsPerPage_;
    std::vector<Record> heap_file_;
    std::vector<int> dataPageIds_;
};

#endif // MINIDB_TABLE_H
