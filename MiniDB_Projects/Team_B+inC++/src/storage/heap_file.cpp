#include "heap_file.hpp"

#include <stdexcept>

#include "buffer_pool.hpp"
#include "disk_manager.hpp"
#include "page.hpp"

HeapFile::HeapFile(BufferPool& pool, DiskManager& disk) : pool_(pool), disk_(disk) {}

RowID HeapFile::insert(const std::string& tuple) {
    // first-fit: scan pages for room
    for (PageID pid = 0; pid < disk_.num_pages(); ++pid) {
        char* data = pool_.fetch_page(pid);
        SlottedPage page(data);
        std::optional<SlotId> slot = page.insert(tuple);
        if (slot) {
            pool_.unpin_page(pid, /*dirty=*/true);
            return RowID{pid, *slot};
        }
        pool_.unpin_page(pid, /*dirty=*/false);
    }

    // none had room: new page
    PageID pid = pool_.new_page();
    char* data = pool_.fetch_page(pid);
    SlottedPage page(data);
    page.init();
    std::optional<SlotId> slot = page.insert(tuple);
    pool_.unpin_page(pid, /*dirty=*/true);
    if (!slot) throw std::runtime_error("HeapFile::insert: tuple too large for a single page");
    return RowID{pid, *slot};
}

std::optional<std::string> HeapFile::get(RowID rid) {
    if (rid.page_id < 0 || rid.page_id >= disk_.num_pages()) return std::nullopt;
    char* data = pool_.fetch_page(rid.page_id);
    SlottedPage page(data);
    std::optional<std::string> value = page.get(rid.slot);
    pool_.unpin_page(rid.page_id, /*dirty=*/false);
    return value;
}

bool HeapFile::erase(RowID rid) {
    if (rid.page_id < 0 || rid.page_id >= disk_.num_pages()) return false;
    char* data = pool_.fetch_page(rid.page_id);
    SlottedPage page(data);
    bool ok = page.erase(rid.slot);
    pool_.unpin_page(rid.page_id, /*dirty=*/ok);
    return ok;
}

std::vector<std::pair<RowID, std::string>> HeapFile::scan() {
    std::vector<std::pair<RowID, std::string>> out;
    for (PageID pid = 0; pid < disk_.num_pages(); ++pid) {
        char* data = pool_.fetch_page(pid);
        SlottedPage page(data);
        for (SlotId s = 0; s < page.slot_count(); ++s) {
            std::optional<std::string> value = page.get(s);
            if (value) out.emplace_back(RowID{pid, s}, *value);
        }
        pool_.unpin_page(pid, /*dirty=*/false);
    }
    return out;
}
