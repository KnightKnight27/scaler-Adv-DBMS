#include "storage/heap_file.h"

namespace minidb {

HeapFile::HeapFile(PageManager* page_manager, BufferPool* buffer_pool, int first_page_id)
    : page_manager_(page_manager),
      buffer_pool_(buffer_pool),
      first_page_id_(first_page_id),
      last_page_id_(first_page_id) {}

Rid HeapFile::InsertTuple(const Row& row) {
    if (first_page_id_ >= page_manager_->PageCount()) {
        int new_page = AllocatePage();
        first_page_id_ = new_page;
        last_page_id_ = new_page;
    }
    for (int page_id = first_page_id_; page_id <= last_page_id_; ++page_id) {
        if (auto rid = InsertInPage(page_id, row)) {
            return *rid;
        }
    }
    int new_page = AllocatePage();
    last_page_id_ = new_page;
    auto rid = InsertInPage(new_page, row);
    if (!rid) {
        throw std::runtime_error("Failed to insert into new heap page");
    }
    return *rid;
}

std::optional<Row> HeapFile::GetTuple(const Rid& rid) const {
    if (rid.page_id < 0 || rid.page_id >= page_manager_->PageCount()) {
        return std::nullopt;
    }
    Page* page = buffer_pool_->FetchPage(rid.page_id);
    auto raw = page->GetTuple(rid.slot_id);
    buffer_pool_->UnpinPage(rid.page_id, false);
    if (!raw) {
        return std::nullopt;
    }
    Row row = Page::DeserializeRow(*raw);
    row.rid = rid;
    return row;
}

bool HeapFile::DeleteTuple(const Rid& rid) {
    if (rid.page_id < 0 || rid.page_id >= page_manager_->PageCount()) {
        return false;
    }
    Page* page = buffer_pool_->FetchPage(rid.page_id);
    bool ok = page->DeleteTuple(rid.slot_id);
    buffer_pool_->UnpinPage(rid.page_id, ok);
    return ok;
}

std::vector<Rid> HeapFile::ScanAll() const {
    std::vector<Rid> rids;
    for (int page_id = first_page_id_; page_id <= last_page_id_; ++page_id) {
        Page* page = buffer_pool_->FetchPage(page_id);
        for (int slot : page->ValidSlots()) {
            rids.push_back(Rid{page_id, slot});
        }
        buffer_pool_->UnpinPage(page_id, false);
    }
    return rids;
}

std::optional<Rid> HeapFile::InsertInPage(int page_id, const Row& row) {
    Page* page = buffer_pool_->FetchPage(page_id);
    auto raw = Page::SerializeRow(row);
    auto slot = page->InsertTuple(raw);
    if (!slot) {
        buffer_pool_->UnpinPage(page_id, false);
        return std::nullopt;
    }
    buffer_pool_->UnpinPage(page_id, true);
    return Rid{page_id, *slot};
}

int HeapFile::AllocatePage() {
    int page_id = page_manager_->AllocatePage();
    Page* page = buffer_pool_->FetchPage(page_id);
    page->Initialize();
    buffer_pool_->UnpinPage(page_id, true);
    return page_id;
}

}  // namespace minidb
