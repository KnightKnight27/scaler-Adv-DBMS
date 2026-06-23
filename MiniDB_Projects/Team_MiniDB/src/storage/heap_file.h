#pragma once

#include <string>

#include "common/types.h"
#include "storage/buffer_pool.h"

namespace minidb {

// HeapFile stores a table's rows as an unordered collection ("heap") spread over
// a singly-linked chain of slotted pages (each page's next_page_id points to the
// next). Rows are addressed by RID = (page_id, slot). This is the Postgres-style
// heap taught in class: rows go wherever there is room; ordering, when needed,
// comes from a separate index (M2).
class HeapFile {
public:
    HeapFile(BufferPool* bp, PageId first_page) : bp_(bp), first_page_(first_page) {}

    // Allocate and initialise an empty heap, returning its first page id (to be
    // recorded in the catalog by higher layers).
    static PageId create(BufferPool* bp);

    PageId first_page() const { return first_page_; }

    // Insert a record's bytes; returns its RID. Grows the chain if no page has room.
    RID insert(const std::string& record);

    // Read the record at `rid` into `out`. Returns false if the slot is erased.
    bool get(RID rid, std::string& out);

    // Erase the record at `rid`. Returns false if already erased / invalid.
    bool erase(RID rid);

    // Forward cursor over all live records in the heap.
    class Iterator {
    public:
        Iterator(BufferPool* bp, PageId start) : bp_(bp), pid_(start) {}
        // Fetch the next live record into (out_rid, out); false when exhausted.
        bool next(RID& out_rid, std::string& out);
    private:
        BufferPool*  bp_;
        PageId       pid_;
        std::int16_t slot_ = 0;
    };

    Iterator begin() { return Iterator(bp_, first_page_); }

private:
    BufferPool* bp_;
    PageId      first_page_;
};

} // namespace minidb
