#pragma once

#include "common/types.h"
#include "storage/buffer_pool.h"

#include <functional>
#include <vector>

// ============================================================
// HeapFile — stores rows for a single table as a linked list of pages
//
// Each table gets its own HeapFile. The first page is the "header page"
// which tracks: first_data_page_id and row count.
//
// Linked list: each data page's next_page_id points to the next,
// last page's next_page_id = INVALID_PAGE_ID.
//
// For inserts, we scan pages to find one with enough free space.
// If none, allocate a new page and link it at the end.
// ============================================================

class HeapFile {
public:
    // Create a HeapFile that uses the given buffer pool.
    // first_page_id is the starting page of this table's linked list.
    // If first_page_id == INVALID_PAGE_ID, the heap file hasn't been
    // created yet — call Create() first.
    HeapFile(BufferPool* pool, int first_page_id = INVALID_PAGE_ID);

    // Allocate the first page for this heap file. Returns first_page_id.
    int Create();

    // Insert a serialized row. Returns the RID where it was stored.
    RID InsertRow(const char* row_data, int row_size);

    // Get a row by RID. Returns false if the slot is empty/deleted.
    bool GetRow(const RID& rid, char* out_data, int* out_size);

    // Delete a row by RID. Returns false if already deleted.
    bool DeleteRow(const RID& rid);

    // Update a row by RID. If new data doesn't fit in the same slot,
    // deletes from old location and inserts at a new one (forwarding).
    // Returns the (possibly new) RID.
    RID UpdateRow(const RID& rid, const char* new_data, int new_size);

    // Scan all rows, calling visitor(rid, data, size) for each live row.
    // This is the full table scan used by SeqScanNode.
    void Scan(std::function<void(const RID&, const char*, int)> visitor);

    int GetFirstPageId() const { return first_page_id_; }

private:
    BufferPool* pool_;
    int first_page_id_;

    // Find a page with at least required_space free bytes.
    // If none exists, allocates a new page and links it.
    // Returns page_id of the page with space.
    int FindPageWithSpace(int required_space);
};
