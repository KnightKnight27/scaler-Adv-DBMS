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

HeapFile::Iterator::~Iterator() = default;

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

    // Walk the chain. The first page is given by TableInfo::firstPageId;
    // every subsequent page is reached by following `nextPage`. If we
    // reach a page with nextPage == 0 we know we've found the tail and we
    // may append. If a page is full we allocate a new page, link the old
    // tail's nextPage to the new id, and continue.
    PageId curr = info_->firstPageId;
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
                return bp_->unpinPage(curr, true);
            }
        }
        // Page is full (or had no free slot at all). If it already has a
        // successor in the chain, just follow it.
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
    it->bp_   = bp_;
    it->page_ = info_->firstPageId;
    it->slot_ = 0;
    return it;
}

bool HeapFile::Iterator::next(RecordId& rid, std::span<const std::uint8_t>& bytes) {
    if (bp_ == nullptr || page_ == 0) return false;
    while (true) {
        Page* p = nullptr;
        if (bp_->fetchPage(page_, p) != Status::OK) return false;
        if (p == nullptr) {
            (void)bp_->unpinPage(page_, false);
            return false;
        }
        // We need the slot count to know when to advance pages. We don't
        // unpin until we find a live record or we exhaust the chain.
        const std::uint16_t n = p->slotCount();
        while (slot_ < n) {
            const std::uint16_t here = slot_;
            ++slot_;
            auto sp = p->getRecord(here);
            if (!sp.empty()) {
                rid = makeRid(page_, here);
                bytes = sp;
                return true;  // we keep this page pinned; close() will unpin
            }
        }
        // Out of slots on this page. Follow the chain via nextPage; 0
        // means "end of chain" and the iteration is done.
        const PageId next = p->nextPage();
        (void)bp_->unpinPage(page_, false);
        if (next == INVALID_PAGE_ID) {
            page_ = 0;
            return false;
        }
        page_ = next;
        slot_ = 0;
        // Loop and re-fetch the new page.
    }
}

void HeapFile::Iterator::close() {
    if (bp_ == nullptr) return;
    if (page_ != 0) {
        (void)bp_->unpinPage(page_, false);
        page_ = 0;
    }
}

} // namespace minidb::storage