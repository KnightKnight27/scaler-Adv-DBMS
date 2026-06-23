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

// ─── Record ───────────────────────────────────────────────────────────────────
//
// A Record is what the heap layer stores and retrieves.
// For simplicity, each record has an integer key and a string value.
// Both are stored as raw bytes inside the page body.
//
// Binary layout inside the page body slot:
//   [4 bytes: key (int32_t)] [N bytes: value string, null-terminated]

struct Record {
    int32_t     key   = 0;
    std::string value;

    // Serialize into a byte buffer (for writing into a page slot).
    // Returns the number of bytes written.
    size_t serialize(char* buf, size_t buf_size) const;

    // Deserialize from a byte buffer (for reading from a page slot).
    static Record deserialize(const char* buf, size_t len);
};

// ─── HeapFile ─────────────────────────────────────────────────────────────────
//
// A HeapFile manages a collection of pages for one table.
// Records are stored in slotted pages via the BufferPool.
//
// Operations:
//   insertRecord  — finds a page with enough free space, writes the record,
//                   returns the RecordID (page_id + slot_id)
//   getRecord     — reads a record given its RecordID
//   deleteRecord  — marks a slot as deleted (sets length = 0)
//   scanAll       — iterates over all live (non-deleted) records
//
// The HeapFile stores a "catalog" of which pages belong to it.
// For simplicity, we track this as a contiguous range: [first_page_, last_page_].

class HeapFile {
public:
    HeapFile(const std::string& table_name, BufferPool& bp);

    // Initialize a brand-new heap (allocates the first data page).
    void create();

    // Open an existing heap (given the first page ID was previously allocated).
    void open(PageID first_page_id);

    // Insert a record. Returns the RecordID of the newly inserted record.
    // Returns an invalid RecordID on failure (e.g., record too large).
    RecordID insertRecord(const Record& rec);

    // Read a record by its RecordID.
    // Returns std::nullopt if the slot is deleted or RecordID is invalid.
    std::optional<Record> getRecord(const RecordID& rid);

    // Mark a record as deleted.
    // Returns true on success.
    bool deleteRecord(const RecordID& rid);

    // Iterate over all live records.
    // Calls callback(rid, record) for each. Stops if callback returns false.
    void scanAll(std::function<bool(const RecordID&, const Record&)> callback);

    // The PageID of the first page in this heap (used to persist/reopen the table).
    PageID firstPageId() const { return first_page_id_; }

    const std::string& tableName() const { return table_name_; }

    // Number of live records (approximate — does not subtract deleted slots).
    size_t recordCount() const { return record_count_; }

private:
    // Find a page with at least `space_needed` bytes free.
    // Allocates a new page if necessary.
    // Returns INVALID_PAGE_ID on failure.
    PageID findOrAllocatePage(size_t space_needed);

    // Write record data into a page at the next available slot.
    // Returns the slot_id used, or INVALID_SLOT_ID on failure.
    SlotID writeToPage(Page* page, const char* data, uint16_t data_len);

    std::string  table_name_;
    BufferPool&  bp_;
    PageID       first_page_id_  = INVALID_PAGE_ID;
    PageID       last_page_id_   = INVALID_PAGE_ID;
    size_t       record_count_   = 0;
};
