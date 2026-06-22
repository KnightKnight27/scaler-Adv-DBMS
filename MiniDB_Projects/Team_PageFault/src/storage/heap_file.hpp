#pragma once
#include <vector>
#include <string>
#include <functional>
#include "../common/types.hpp"
#include "buffer_pool.hpp"
#include "page.hpp"

namespace minidb {

// An unordered collection of slotted pages making up one table. The page list
// is owned by the catalog (so it can be persisted); the heap file borrows it
// and calls on_grow_ whenever it appends a page so the catalog stays durable.
class HeapFile {
public:
    HeapFile(BufferPool& bp, std::vector<PageId>& pages, std::function<void()> on_grow)
        : bp_(bp), pages_(pages), on_grow_(std::move(on_grow)) {}

    // First-fit insert of raw tuple bytes. Returns the new RowId.
    RowId insert(const char* data, uint16_t len);

    // Fetch a tuple by RowId; false if the slot is empty/deleted.
    bool get(RowId rid, std::string& out) const;

    // Tombstone a tuple by RowId.
    void erase(RowId rid);

    // Visit every live tuple. The callback gets (RowId, bytes, len).
    void scan(const std::function<void(RowId, const char*, uint16_t)>& fn) const;

    size_t pageCount() const { return pages_.size(); }

private:
    BufferPool& bp_;
    std::vector<PageId>& pages_;
    std::function<void()> on_grow_;
};

} // namespace minidb
