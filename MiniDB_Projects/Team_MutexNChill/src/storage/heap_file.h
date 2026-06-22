#pragma once
#include "page.h"
#include "page_manager.h"
#include "buffer_pool.h"
#include <vector>
#include <string>

// A RID (Record ID) is the physical address of one row:
// which page it lives on, and which slot within that page.
struct RID {
    int page_id;
    int slot;
};

// HeapFile provides record-level operations (insert / delete / scan).
// Internally it uses a BufferPool to avoid re-reading pages from disk.
// Pages store rows in a simple append-only slot array.
class HeapFile {
public:
    explicit HeapFile(const std::string& filename);
    ~HeapFile();

    // Add a row. Returns the RID of the newly inserted row.
    RID  insertRow(const Row& row);

    // Mark a row as deleted (sets is_valid = false). Does not compact.
    void deleteRow(const RID& rid);

    // Read one row by its RID.
    Row  getRow(const RID& rid);

    // Return every is_valid row in the file (full table scan).
    std::vector<Row> scanAll();

    // Flush all dirty pages to disk.
    void flush();

    int pageCount() { return pm.pageCount(); }

private:
    PageManager pm;
    BufferPool  bp;
};
