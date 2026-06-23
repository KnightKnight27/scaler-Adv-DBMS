#include "storage/heap_file.h"

#include "common/exception.h"
#include "storage/slotted_page.h"

namespace minidb {

PageId HeapFile::create(BufferPool* bp) {
    PageId pid;
    Page* page = bp->new_page(pid);
    SlottedPage(page->data()).init();
    bp->unpin_page(pid, /*dirty=*/true);
    return pid;
}

RID HeapFile::insert(const std::string& record) {
    if (record.size() > PAGE_SIZE - SlottedPage::HEADER_SIZE - SlottedPage::SLOT_SIZE)
        throw DBException("HeapFile: record too large for a page");

    PageId pid = first_page_;
    PageId tail = first_page_;

    // Walk the chain looking for a page with room.
    while (pid != INVALID_PAGE_ID) {
        Page* page = bp_->fetch_page(pid);
        SlottedPage sp(page->data());
        std::int16_t slot;
        if (sp.insert(record.data(), static_cast<std::uint16_t>(record.size()), slot)) {
            bp_->unpin_page(pid, /*dirty=*/true);
            return RID{pid, slot};
        }
        tail = pid;
        PageId next = sp.next_page();
        bp_->unpin_page(pid, /*dirty=*/false);
        pid = next;
    }

    // No room anywhere: append a fresh page to the tail of the chain.
    PageId new_pid;
    Page* np = bp_->new_page(new_pid);
    SlottedPage nsp(np->data());
    nsp.init();
    std::int16_t slot;
    if (!nsp.insert(record.data(), static_cast<std::uint16_t>(record.size()), slot))
        throw DBException("HeapFile: record does not fit in a fresh page");
    bp_->unpin_page(new_pid, /*dirty=*/true);

    Page* tail_page = bp_->fetch_page(tail);
    SlottedPage(tail_page->data()).set_next_page(new_pid);
    bp_->unpin_page(tail, /*dirty=*/true);

    return RID{new_pid, slot};
}

RID HeapFile::append(const std::string& record, PageId& tail) {
    if (record.size() > PAGE_SIZE - SlottedPage::HEADER_SIZE - SlottedPage::SLOT_SIZE)
        throw DBException("HeapFile: record too large for a page");
    if (tail == INVALID_PAGE_ID) tail = first_page_;

    // Try the current tail page first.
    Page* page = bp_->fetch_page(tail);
    SlottedPage sp(page->data());
    std::int16_t slot;
    if (sp.insert(record.data(), static_cast<std::uint16_t>(record.size()), slot)) {
        bp_->unpin_page(tail, /*dirty=*/true);
        return RID{tail, slot};
    }
    bp_->unpin_page(tail, /*dirty=*/false);

    // Tail is full: allocate a new page and link it on.
    PageId new_pid;
    Page* np = bp_->new_page(new_pid);
    SlottedPage nsp(np->data());
    nsp.init();
    if (!nsp.insert(record.data(), static_cast<std::uint16_t>(record.size()), slot))
        throw DBException("HeapFile: record does not fit in a fresh page");
    bp_->unpin_page(new_pid, /*dirty=*/true);

    Page* tail_page = bp_->fetch_page(tail);
    SlottedPage(tail_page->data()).set_next_page(new_pid);
    bp_->unpin_page(tail, /*dirty=*/true);

    tail = new_pid;
    return RID{new_pid, slot};
}

bool HeapFile::get(RID rid, std::string& out) {
    Page* page = bp_->fetch_page(rid.page_id);
    bool ok = SlottedPage(page->data()).get(rid.slot, out);
    bp_->unpin_page(rid.page_id, /*dirty=*/false);
    return ok;
}

bool HeapFile::erase(RID rid) {
    Page* page = bp_->fetch_page(rid.page_id);
    bool ok = SlottedPage(page->data()).erase(rid.slot);
    bp_->unpin_page(rid.page_id, /*dirty=*/ok);
    return ok;
}

bool HeapFile::Iterator::next(RID& out_rid, std::string& out) {
    while (pid_ != INVALID_PAGE_ID) {
        Page* page = bp_->fetch_page(pid_);
        SlottedPage sp(page->data());
        std::int16_t n = static_cast<std::int16_t>(sp.slot_count());
        while (slot_ < n) {
            std::int16_t cur = slot_++;
            if (sp.get(cur, out)) {
                out_rid = RID{pid_, cur};
                bp_->unpin_page(pid_, /*dirty=*/false);
                return true;
            }
        }
        PageId next = sp.next_page();
        bp_->unpin_page(pid_, /*dirty=*/false);
        pid_ = next;
        slot_ = 0;
    }
    return false;
}

} // namespace minidb
