#pragma once

#include "common/types.h"
#include "storage/buffer_pool.h"
#include <functional>
#include <string>
#include <sstream>
#include <vector>

namespace minidb {

/*
 * HeapFile: manages a table as a linked list of pages.
 * Records are stored as serialized rows (pipe-delimited text for simplicity).
 */
class HeapFile {
public:
    HeapFile(const std::string& table_name, BufferPoolManager* bpm,
             const Schema& schema);

    // Insert a row, returns its RecordId
    RecordId InsertTuple(const Row& row);

    // Get a row by RecordId
    bool GetTuple(const RecordId& rid, Row& out);

    // Delete a row by RecordId
    bool DeleteTuple(const RecordId& rid);

    // Scan all valid tuples, calling callback for each
    void ScanAll(std::function<void(const RecordId&, const Row&)> callback);

    // Get the first page id
    page_id_t GetFirstPageId() const { return first_page_id_; }

    // Number of tuples (approximate, for optimizer)
    uint32_t GetTupleCount() const { return tuple_count_; }

    void Flush() { bpm_->FlushAll(); }

    const Schema& GetSchema() const { return schema_; }
    const std::string& GetTableName() const { return table_name_; }

    // Serialize a Row to bytes
    static std::string SerializeRow(const Row& row, const Schema& schema);
    // Deserialize bytes to a Row
    static Row DeserializeRow(const char* data, uint16_t len, const Schema& schema);

private:
    std::string        table_name_;
    BufferPoolManager* bpm_;
    Schema             schema_;
    page_id_t          first_page_id_ = INVALID_PAGE_ID;
    page_id_t          last_page_id_  = INVALID_PAGE_ID;
    uint32_t           tuple_count_   = 0;

    // Find a page with enough free space, or allocate a new one
    Page* FindOrAllocatePage(uint16_t needed_size, page_id_t& out_page_id);
};

} // namespace minidb
