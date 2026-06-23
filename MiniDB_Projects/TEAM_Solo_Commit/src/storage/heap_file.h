// MiniDB - HeapFile: an unordered ("heap") collection of pages holding one table's rows.
// Inserts go to the last page, allocating a new one when it is full. Deletes tombstone the
// slot. A forward Iterator walks every live (page, slot) for sequential scans.
#pragma once

#include <string>
#include <vector>

#include "../common/rid.h"
#include "buffer_pool.h"

namespace minidb {

class HeapFile {
public:
    explicit HeapFile(BufferPool* bp) : bp_(bp) {}
    HeapFile(BufferPool* bp, std::vector<int> pages) : bp_(bp), page_ids_(std::move(pages)) {}

    RID Insert(const std::string& rec);
    bool Get(RID rid, std::string* out) const;
    bool Delete(RID rid);

    const std::vector<int>& Pages() const { return page_ids_; }
    size_t NumPages() const { return page_ids_.size(); }

    // Forward scan over all live records. Fetches/unpins per step (simple, not the fastest).
    class Iterator {
    public:
        Iterator(const HeapFile* hf, size_t page_pos, int slot)
            : hf_(hf), page_pos_(page_pos), slot_(slot) { SkipToLive(); }

        bool operator!=(const Iterator& o) const {
            return page_pos_ != o.page_pos_ || slot_ != o.slot_;
        }
        Iterator& operator++() { ++slot_; SkipToLive(); return *this; }

        RID GetRID() const {
            return RID(hf_->page_ids_[page_pos_], slot_);
        }
        std::string GetRecord() const {
            std::string out;
            hf_->Get(GetRID(), &out);
            return out;
        }

    private:
        void SkipToLive();  // advance (page_pos_, slot_) to the next live slot or to end
        const HeapFile* hf_;
        size_t page_pos_;
        int slot_;
    };

    Iterator begin() const { return Iterator(this, 0, 0); }
    Iterator end() const { return Iterator(this, page_ids_.size(), 0); }

private:
    BufferPool* bp_;
    std::vector<int> page_ids_;
};

}  // namespace minidb
