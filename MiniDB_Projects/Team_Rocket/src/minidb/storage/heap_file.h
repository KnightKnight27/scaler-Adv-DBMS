#pragma once

#include <cstdint>
#include <vector>

#include "../types.h"
#include "buffer_pool.h"
#include "page.h"

namespace minidb {

// A heap of tuples spread across a list of pages owned by one table. The page
// list lives in the catalog and is shared by reference so it persists.
class HeapFile {
public:
    HeapFile(BufferPool& bp, std::vector<int>& page_ids) : bp_(bp), pages_(page_ids) {}

    RID insert(const std::vector<uint8_t>& bytes) {
        // Append into the most recent page; allocate a new one when it fills.
        // This keeps insert O(1) instead of rescanning the whole heap each time.
        if (!pages_.empty()) {
            int pid = pages_.back();
            Page p = bp_.fetch_page(pid);
            int s = p.insert(bytes.data(), static_cast<int>(bytes.size()));
            bp_.unpin(pid, s >= 0);
            if (s >= 0) return RID{pid, s};
        }
        int pid;
        Page p = bp_.new_page(pid);
        pages_.push_back(pid);
        int s = p.insert(bytes.data(), static_cast<int>(bytes.size()));
        bp_.unpin(pid, true);
        return RID{pid, s};
    }

    bool get(const RID& rid, std::vector<uint8_t>& out) {
        Page p = bp_.fetch_page(rid.page_id);
        const uint8_t* d;
        int len;
        bool ok = p.get(rid.slot_id, d, len);
        if (ok) out.assign(d, d + len);
        bp_.unpin(rid.page_id, false);
        return ok;
    }

    void mark_delete(const RID& rid) {
        Page p = bp_.fetch_page(rid.page_id);
        p.mark_delete(rid.slot_id);
        bp_.unpin(rid.page_id, true);
    }

    // Replay a logged insert at its original RID (recovery only).
    void put_at(const RID& rid, const std::vector<uint8_t>& bytes) {
        Page p = bp_.fetch_page(rid.page_id);
        p.ensure_init();
        p.put_at(rid.slot_id, bytes.data(), static_cast<int>(bytes.size()));
        bp_.unpin(rid.page_id, true);
    }

    void set_lsn(int page_id, int64_t lsn) { bp_.set_lsn(page_id, lsn); }

    template <class F>
    void scan(F&& fn) {
        for (int pid : pages_) {
            Page p = bp_.fetch_page(pid);
            int ns = p.num_slots();
            for (int s = 0; s < ns; ++s) {
                const uint8_t* d;
                int len;
                if (p.get(s, d, len)) {
                    std::vector<uint8_t> b(d, d + len);
                    fn(RID{pid, s}, b);
                }
            }
            bp_.unpin(pid, false);
        }
    }

private:
    BufferPool& bp_;
    std::vector<int>& pages_;
};

}  // namespace minidb
