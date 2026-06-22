#include "heap_file.hpp"
#include <stdexcept>

namespace minidb {

RowId HeapFile::insert(const char* data, uint16_t len) {
    // Reject up front anything that cannot fit in an empty page, so we never
    // allocate/grow on a doomed insert.
    if (static_cast<size_t>(len) + SlottedPage::HEADER_SIZE + SlottedPage::SLOT_SIZE > PAGE_SIZE)
        throw std::runtime_error("tuple too large for a page");

    // First-fit: try existing pages in order.
    for (PageId pid : pages_) {
        char* raw = bp_.fetchPage(pid);
        SlottedPage page(raw);
        // A page recovered from disk as all-zeros (allocated but never flushed
        // before a crash) must be initialized before reuse.
        if (!page.initialized()) page.init();
        int slot = page.insertTuple(data, len);
        if (slot >= 0) {
            bp_.unpinPage(pid, /*dirty=*/true);
            return RowId{pid, static_cast<int16_t>(slot)};
        }
        bp_.unpinPage(pid, /*dirty=*/false);
    }
    // None fit: grow the heap by one page.
    PageId pid;
    char* raw = bp_.newPage(pid);
    SlottedPage page(raw);
    page.init();
    int slot = page.insertTuple(data, len);
    bp_.unpinPage(pid, /*dirty=*/true);
    pages_.push_back(pid);
    if (on_grow_) on_grow_();
    return RowId{pid, static_cast<int16_t>(slot)};
}

bool HeapFile::get(RowId rid, std::string& out) const {
    if (!rid.valid()) return false;
    char* raw = bp_.fetchPage(rid.page);
    SlottedPage page(raw);
    const char* bytes;
    uint16_t len;
    bool ok = page.getTuple(rid.slot, bytes, len);
    if (ok) out.assign(bytes, len);
    bp_.unpinPage(rid.page, /*dirty=*/false);
    return ok;
}

void HeapFile::erase(RowId rid) {
    if (!rid.valid()) return;
    char* raw = bp_.fetchPage(rid.page);
    SlottedPage page(raw);
    page.eraseTuple(rid.slot);
    bp_.unpinPage(rid.page, /*dirty=*/true);
}

void HeapFile::scan(const std::function<void(RowId, const char*, uint16_t)>& fn) const {
    for (PageId pid : pages_) {
        char* raw = bp_.fetchPage(pid);
        SlottedPage page(raw);
        uint16_t n = page.numSlots();
        for (uint16_t s = 0; s < n; s++) {
            const char* bytes;
            uint16_t len;
            if (page.getTuple(s, bytes, len))
                fn(RowId{pid, static_cast<int16_t>(s)}, bytes, len);
        }
        bp_.unpinPage(pid, /*dirty=*/false);
    }
}

} // namespace minidb
