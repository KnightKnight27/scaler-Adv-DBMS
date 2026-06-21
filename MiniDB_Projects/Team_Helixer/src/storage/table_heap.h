#pragma once
#include <vector>
#include <utility>
#include "common/types.h"
#include "storage/buffer_pool.h"
#include "catalog/schema.h"

namespace minidb {

// On-page header for a heap (table) page. Pages of one table form a singly
// linked list via next_page_id so a sequential scan can walk the whole table.
struct TablePageHeader {
    page_id_t next_page_id; // next page in the chain, or INVALID_PAGE_ID
    int32_t   num_slots;    // number of slot-directory entries (incl. tombstones)
    int32_t   free_offset;  // byte offset where free space ends (records grow down)
};

// One slot-directory entry. length == -1 marks a deleted (tombstoned) record.
struct Slot {
    int32_t offset;
    int32_t length;
};

// A TableHeap is the physical storage for one table: a chain of slotted pages.
// Records are variable length; each page stores a slot directory growing from
// the front and record bytes growing from the back, classic slotted-page design.
class TableHeap {
public:
    TableHeap(BufferPoolManager *bpm, const Schema *schema, page_id_t first_page_id)
        : bpm_(bpm), schema_(schema), first_page_id_(first_page_id) {}

    // Insert a tuple; returns its RID. Allocates a new page (extending the
    // chain) when no existing page has room.
    RID insert_tuple(const Tuple &tuple);

    // Insert a tuple at a specific RID's page if possible (used by recovery redo
    // to reproduce the exact physical layout is overkill; recovery instead
    // replays logical inserts). Kept simple: normal insert only.

    // Fetch the tuple at `rid`. Returns false if the slot is deleted/invalid.
    bool get_tuple(const RID &rid, Tuple *out);

    // Tombstone the tuple at `rid`. Returns false if already deleted/invalid.
    bool delete_tuple(const RID &rid);

    // Full sequential scan: returns every live (RID, Tuple) in the table.
    std::vector<std::pair<RID, Tuple>> scan();

    page_id_t first_page_id() const { return first_page_id_; }

private:
    BufferPoolManager *bpm_;
    const Schema      *schema_;
    page_id_t          first_page_id_;                 // INVALID until first insert
    page_id_t          last_page_id_{INVALID_PAGE_ID}; // tail of the chain (insert point)

    // Try to place `record` on the page `page_id`; on success sets *out_rid.
    bool insert_on_page(page_id_t page_id, const std::vector<char> &record, RID *out_rid);
};

} // namespace minidb
