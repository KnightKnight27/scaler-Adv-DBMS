#pragma once

#include "storage/buffer_pool.h"
#include "storage/page.h"
#include "common/types.h"
#include "common/config.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <cstring>

/**
 * @struct Record
 * @brief The data row stored in HeapFile slots.
 *
 * Each row contains a primary integer key and a variable-length string value.
 * Stored sequentially inside a slotted page:
 * Layout: [4 bytes: key (int32_t)] [N bytes: null-terminated value string]
 */
struct Record {
    int32_t key = 0;      ///< Record primary key integer value
    std::string value;    ///< Value payload string

    /**
     * @brief Serializes the record attributes into a destination char buffer.
     * @param buf Target byte destination.
     * @param buf_size Total size of the buffer.
     * @return Number of serialized bytes written.
     */
    size_t serialize(char* buf, size_t buf_size) const;

    /**
     * @brief Deserializes a record from a raw binary buffer.
     * @param buf Source char array.
     * @param len Length of source bytes to scan.
     */
    static Record deserialize(const char* buf, size_t len);
};

/**
 * @class HeapFile
 * @brief Manages a set of database pages for a table storing user records.
 *
 * Tracks the table data starting page firstPageId() and maps records into slotted pages.
 */
class HeapFile {
public:
    /**
     * @brief Construct a new Heap File helper.
     * @param table_name Name of table.
     * @param bp Reference to the active Buffer Pool.
     */
    HeapFile(const std::string& table_name, BufferPool& bp);

    /**
     * @brief Initializes a fresh table space by allocating a starting page.
     */
    void create();

    /**
     * @brief Opens an existing table starting from first_page_id.
     */
    void open(PageID first_page_id);

    /**
     * @brief Inserts a record, returning its generated RecordID.
     * @param rec Record row to insert.
     * @return Unique RecordID on success, invalid RecordID otherwise.
     */
    RecordID insertRecord(const Record& rec);

    /**
     * @brief Resolves a Record by its RecordID.
     * @return Deserialized Record, or std::nullopt if tombstoned.
     */
    std::optional<Record> getRecord(const RecordID& rid);

    /**
     * @brief Deletes a record by its RecordID (marks its slot as tombstoned).
     * @return True on success.
     */
    bool deleteRecord(const RecordID& rid);

    /**
     * @brief Scans all active (non-tombstoned) records in the table.
     * Invokes callback(RecordID, Record) for each row. Iteration ceases if callback yields false.
     */
    void scanAll(std::function<bool(const RecordID&, const Record&)> callback);

    /**
     * @brief Return the starting PageID.
     */
    PageID firstPageId() const { return first_page_id_; }

    /**
     * @brief Returns the table name.
     */
    const std::string& tableName() const { return table_name_; }

    /**
     * @brief High water mark of records in the HeapFile (excluding deletions).
     */
    size_t recordCount() const { return record_count_; }

private:
    /**
     * @brief Checks the last page for space, or allocates a new one if it is full.
     */
    PageID findOrAllocatePage(size_t space_needed);

    /**
     * @brief Copies data into a new slot at the end of the slotted page layout.
     * @return Generated SlotID, or INVALID_SLOT_ID on failure.
     */
    SlotID writeToPage(Page* page, const char* data, uint16_t data_len);

    std::string table_name_;            ///< Table identity name
    BufferPool& bp_;                    ///< Associated memory cache buffer pool
    PageID first_page_id_ = INVALID_PAGE_ID; ///< First page containing table rows
    PageID last_page_id_ = INVALID_PAGE_ID;  ///< Last allocated page in heap sequence
    size_t record_count_ = 0;           ///< Approximate record count tracker
};
