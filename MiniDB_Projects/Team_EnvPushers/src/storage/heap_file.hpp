// Heap file: an unordered collection of records spread across linked pages.
//
// Each table owns one heap file: a singly-linked list of slotted pages (via
// Page::next_page_id). A record is addressed by an RID = (page_id, slot_id)
// that stays stable for the record's lifetime, so indexes can point at it.
// All page access goes through the buffer pool.
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "common/types.hpp"
#include "storage/buffer_pool.hpp"

namespace minidb {

class HeapFile {
public:
    HeapFile(BufferPool* bp, PageId first_page_id)
        : bp_(bp), first_page_id_(first_page_id) {}

    // Create a fresh, empty heap file; returns the first page id via out param.
    static HeapFile create(BufferPool* bp, PageId* out_first_page_id);

    PageId first_page_id() const { return first_page_id_; }

    RID insert(const std::vector<uint8_t>& record);
    std::optional<std::vector<uint8_t>> get(const RID& rid);
    bool erase(const RID& rid);
    RID update(const RID& rid, const std::vector<uint8_t>& record);

    // Full sequential scan; calls fn(rid, record) for every live tuple.
    void scan(const std::function<void(const RID&, const std::vector<uint8_t>&)>& fn);

private:
    BufferPool* bp_;
    PageId first_page_id_;
};

}  // namespace minidb
