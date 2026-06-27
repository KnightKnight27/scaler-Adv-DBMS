// =============================================================================
// src/storage/heap_file.cpp
// -----------------------------------------------------------------------------
// Heap file = a chain of pages, each holding a slot directory. The first
// page id of a table is stored in TableInfo::firstPageId; subsequent pages
// in the chain are reached by following the per-page `nextPage` header
// field (see src/storage/page.cpp). 0 marks the end of the chain.
//
// When a page is full and the caller inserts another row, the heap file
// allocates a fresh page via the buffer pool, links the previous page's
// nextPage pointer to the new id, and continues trying to insert.
//
// When the table is freshly created the first page is allocated eagerly
// by CatalogManager::createTable, and its id is written back into the
// TableInfo. HeapFile reads the first page id from the TableInfo
// TableInfo::firstPageId.
// =============================================================================
#include "storage/heap_file.h"

#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "common/record_id.h"
#include "common/types.h"
#include <stdexcept>

namespace minidb::storage {

HeapFile::HeapFile(BufferPool* bp, const catalog::TableInfo* info)
    : bp_(bp), info_(info) {}

HeapFile::~HeapFile() = default;

Status HeapFile::insertRecord(std::span<const std::uint8_t> record, RecordId& outRid) {
    outRid = INVALID_RID;
    if (bp_ == nullptr || info_ == nullptr) return Status::INVALID_ARGUMENT;
    if (info_->firstPageId == 0) {
        // The catalog must allocate the first page before the heap file
        // can be used. We surface this as a usage error.
        return Status::INVALID_ARGUMENT;
    }

    // Free-space hint: start the search from the page where the last
    // successful insert landed (if valid). That page usually still has
    // room, so a typical insert pins one page and is done — amortized
    // O(1) instead of the O(pages) walk from firstPageId. Only when the
    // cached page is full do we walk forward from there to the tail and
    // append a new page. HeapFile instances are short-lived (one per
    // statement), so this per-instance cache needs no persistence.
    PageId curr = (lastInsertPageId_ != INVALID_PAGE_ID) ? lastInsertPageId_
                                                         : info_->firstPageId;
    while (true) {
        Page* page = nullptr;
        Status s = bp_->fetchPage(curr, page);
        if (s != Status::OK) return s;
        if (page == nullptr) { (void)bp_->unpinPage(curr, false); return Status::IO_ERROR; }

        const std::uint16_t freeSlot = page->firstFreeSlot();
        if (freeSlot != 0xFFFF) {
            // Try to insert; if it doesn't fit, fall through and allocate a new page.
            if (page->insertRecord(freeSlot, record)) {
                outRid = makeRid(curr, freeSlot);
                lastInsertPageId_ = curr;   // remember where we just wrote
                return bp_->unpinPage(curr, true);
            }
        }
        // Page is full (or had no free slot, or the record did not fit
        // even though a free slot existed). If it already has a successor
        // in the chain, just follow it.
        const PageId next = page->nextPage();
        if (next != INVALID_PAGE_ID) {
            (void)bp_->unpinPage(curr, false);
            curr = next;
            continue;
        }
        // Tail of the chain: allocate a new page, link it from the current
        // page, and then loop and try the new page.
        const PageId newPid = bp_->allocatePage();
        page->setNextPage(newPid);
        (void)bp_->unpinPage(curr, true);
        curr = newPid;
    }
}

Status HeapFile::getRecord(RecordId rid, std::span<const std::uint8_t>& out) const {
    out = {};
    if (bp_ == nullptr) return Status::INVALID_ARGUMENT;
    const PageId  pid  = ridPage(rid);
    const std::uint16_t slot = ridSlot(rid);
    Page* page = nullptr;
    Status s = bp_->fetchPage(pid, page);
    if (s != Status::OK) return s;
    if (page == nullptr) {
        (void)bp_->unpinPage(pid, false);
        return Status::IO_ERROR;
    }
    out = page->getRecord(slot);
    return bp_->unpinPage(pid, false);
}

Status HeapFile::deleteRecord(RecordId rid) {
    if (bp_ == nullptr) return Status::INVALID_ARGUMENT;
    const PageId  pid  = ridPage(rid);
    const std::uint16_t slot = ridSlot(rid);
    Page* page = nullptr;
    Status s = bp_->fetchPage(pid, page);
    if (s != Status::OK) return s;
    if (page == nullptr) {
        (void)bp_->unpinPage(pid, false);
        return Status::IO_ERROR;
    }
    const bool ok = page->deleteRecord(slot);
    (void)bp_->unpinPage(pid, true);
    return ok ? Status::OK : Status::NOT_FOUND;
}

std::unique_ptr<HeapFile::Iterator> HeapFile::scan() {
    auto it = std::make_unique<Iterator>();
    it->bp_      = bp_;
    it->page_   = info_->firstPageId;
    it->slot_   = 0;
    it->pinned_ = false;
    return it;
}

bool HeapFile::Iterator::next(RecordId& rid, std::span<const std::uint8_t>& bytes) {
    if (bp_ == nullptr || page_ == 0) return false;
    while (true) {
        // Pin the page we are currently parked on exactly once. While we
        // scan its slots it stays pinned, so the `bytes` span we hand back
        // to the caller remains valid until the next call to next() (which
        // may advance to another page) or close().
        if (!pinned_) {
            Page* p = nullptr;
            if (bp_->fetchPage(page_, p) != Status::OK) return false;
            if (p == nullptr) {
                (void)bp_->unpinPage(page_, false);
                page_ = 0;
                return false;
            }
            pagePtr_ = p;
            pinned_  = true;
        }
        const std::uint16_t n = pagePtr_->slotCount();
        while (slot_ < n) {
            const std::uint16_t here = slot_;
            ++slot_;
            auto sp = pagePtr_->getRecord(here);
            if (!sp.empty()) {
                rid = makeRid(page_, here);
                bytes = sp;
                return true;  // page stays pinned; next()/close() will unpin
            }
        }
        // Out of slots on this page. Unpin it once now that we are done
        // with it, then follow the chain via nextPage; 0 means "end of
        // chain" and the iteration is done.
        const PageId next = pagePtr_->nextPage();
        (void)bp_->unpinPage(page_, false);
        pagePtr_ = nullptr;
        pinned_  = false;
        if (next == INVALID_PAGE_ID) {
            page_ = 0;
            return false;
        }
        page_ = next;
        slot_ = 0;
        // Loop: the next iteration will lazily pin the new page.
    }
}

void HeapFile::Iterator::close() {
    if (bp_ == nullptr) return;
    if (pinned_ && page_ != 0) {
        (void)bp_->unpinPage(page_, false);
    }
    pagePtr_ = nullptr;
    pinned_  = false;
    page_    = 0;
}

HeapFile::Iterator::~Iterator() {
    // Guarantee we never leak a pin even if the caller forgets close().
    // Safe to call on a default-constructed (unused) iterator.
    close();
}

} // namespace minidb::storage