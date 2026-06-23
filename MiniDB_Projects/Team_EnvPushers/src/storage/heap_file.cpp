#include "storage/heap_file.hpp"

#include <unordered_set>

namespace minidb {

HeapFile HeapFile::create(BufferPool* bp, PageId* out_first_page_id) {
    Page* p = bp->new_page();
    PageId pid = p->page_id();
    bp->unpin_page(pid, /*dirty=*/true);
    *out_first_page_id = pid;
    return HeapFile(bp, pid);
}

RID HeapFile::insert(const std::vector<uint8_t>& record) {
    PageId pid = first_page_id_;
    PageId last_pid = pid;
    while (pid != INVALID_PAGE_ID) {
        Page* page = bp_->fetch_page(pid);
        auto sid = page->insert(record.data(), static_cast<uint16_t>(record.size()));
        if (sid) {
            RID rid{pid, *sid};
            bp_->unpin_page(pid, /*dirty=*/true);
            return rid;
        }
        last_pid = pid;
        PageId next = page->next_page_id();
        bp_->unpin_page(pid, /*dirty=*/false);
        pid = next;
    }

    // Append a fresh page to the chain.
    Page* np = bp_->new_page();
    PageId new_pid = np->page_id();
    auto sid = np->insert(record.data(), static_cast<uint16_t>(record.size()));
    bp_->unpin_page(new_pid, /*dirty=*/true);

    Page* link = bp_->fetch_page(last_pid);
    link->set_next_page_id(new_pid);
    bp_->unpin_page(last_pid, /*dirty=*/true);
    return RID{new_pid, sid.value()};
}

std::optional<std::vector<uint8_t>> HeapFile::get(const RID& rid) {
    Page* page = bp_->fetch_page(rid.page_id);
    auto rec = page->get(rid.slot_id);
    bp_->unpin_page(rid.page_id, /*dirty=*/false);
    return rec;
}

bool HeapFile::erase(const RID& rid) {
    Page* page = bp_->fetch_page(rid.page_id);
    bool ok = page->erase(rid.slot_id);
    bp_->unpin_page(rid.page_id, /*dirty=*/ok);
    return ok;
}

RID HeapFile::update(const RID& rid, const std::vector<uint8_t>& record) {
    Page* page = bp_->fetch_page(rid.page_id);
    bool ok = page->update_inplace(rid.slot_id, record.data(),
                                   static_cast<uint16_t>(record.size()));
    bp_->unpin_page(rid.page_id, /*dirty=*/ok);
    if (ok) return rid;
    // Did not fit in place: delete then re-insert (yields a new RID).
    erase(rid);
    return insert(record);
}

void HeapFile::scan(
    const std::function<void(const RID&, const std::vector<uint8_t>&)>& fn) {
    PageId pid = first_page_id_;
    std::unordered_set<PageId> visited;   // guard against a torn/garbage chain
    while (pid != INVALID_PAGE_ID && pid >= 0) {
        if (!visited.insert(pid).second) break;   // cycle -> stop defensively
        Page* page = bp_->fetch_page(pid);
        uint16_t n = page->num_slots();
        for (SlotId sid = 0; sid < n; ++sid) {
            auto rec = page->get(sid);
            if (rec) fn(RID{pid, sid}, *rec);
        }
        PageId next = page->next_page_id();
        bp_->unpin_page(pid, /*dirty=*/false);
        pid = next;
    }
}

}  // namespace minidb
