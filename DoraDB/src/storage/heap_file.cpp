#include "storage/heap_file.h"
#include <cstring>
#include <iostream>

HeapFile::HeapFile(BufferPool* pool, int first_page_id)
    : pool_(pool), first_page_id_(first_page_id) {}

// ============================================================
// Create — allocate the first page for this table
// ============================================================

int HeapFile::Create() {
    int page_id;
    Page* page = pool_->NewPage(page_id);
    page->Init(page_id);
    pool_->UnpinPage(page_id, true);
    first_page_id_ = page_id;
    return page_id;
}

// ============================================================
// InsertRow — find a page with space and insert the row
// ============================================================

RID HeapFile::InsertRow(const char* row_data, int row_size) {
    int target_page_id = FindPageWithSpace(row_size + SLOT_SIZE);

    Page* page = pool_->FetchPage(target_page_id);
    int slot_id = page->InsertRow(row_data, row_size);

    if (slot_id == -1) {
        // Shouldn't happen since FindPageWithSpace checked, but handle gracefully
        pool_->UnpinPage(target_page_id, false);
        throw std::runtime_error("HeapFile::InsertRow: unexpected no space");
    }

    pool_->UnpinPage(target_page_id, true);  // we modified the page

    RID rid;
    rid.page_id = target_page_id;
    rid.slot_id = slot_id;
    return rid;
}

// ============================================================
// GetRow — fetch a row by its RID
// ============================================================

bool HeapFile::GetRow(const RID& rid, char* out_data, int* out_size) {
    Page* page = pool_->FetchPage(rid.page_id);
    bool found = page->GetRow(rid.slot_id, out_data, out_size);
    pool_->UnpinPage(rid.page_id, false);  // read-only, not dirty
    return found;
}

// ============================================================
// DeleteRow — mark a row's slot as deleted
// ============================================================

bool HeapFile::DeleteRow(const RID& rid) {
    Page* page = pool_->FetchPage(rid.page_id);
    bool ok = page->DeleteRow(rid.slot_id);
    pool_->UnpinPage(rid.page_id, ok);  // dirty only if we actually deleted
    return ok;
}

// ============================================================
// UpdateRow — update in place if fits, otherwise delete + reinsert
// ============================================================

RID HeapFile::UpdateRow(const RID& rid, const char* new_data, int new_size) {
    Page* page = pool_->FetchPage(rid.page_id);

    // Try in-place update first
    if (page->UpdateRow(rid.slot_id, new_data, new_size)) {
        pool_->UnpinPage(rid.page_id, true);
        return rid;  // same RID, updated in place
    }

    // Doesn't fit — delete from old location
    page->DeleteRow(rid.slot_id);
    pool_->UnpinPage(rid.page_id, true);

    // Insert at a new location
    // NOTE: In a full system, we'd store a forwarding pointer at the old slot
    // so B+Tree entries don't need updating. For now, the caller (HeapEngine)
    // is responsible for updating any index entries.
    return InsertRow(new_data, new_size);
}

// ============================================================
// Scan — visit every live row across all pages
// ============================================================

void HeapFile::Scan(std::function<void(const RID&, const char*, int)> visitor) {
    int current_page_id = first_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = pool_->FetchPage(current_page_id);
        int num_slots = page->GetNumSlots();

        for (int slot = 0; slot < num_slots; slot++) {
            char buf[PAGE_SIZE];
            int size;
            if (page->GetRow(slot, buf, &size)) {
                RID rid;
                rid.page_id = current_page_id;
                rid.slot_id = slot;
                visitor(rid, buf, size);
            }
        }

        int next = page->GetNextPageId();
        pool_->UnpinPage(current_page_id, false);
        current_page_id = next;
    }
}

// ============================================================
// FindPageWithSpace — walk the linked list looking for room
// ============================================================

int HeapFile::FindPageWithSpace(int required_space) {
    int current_page_id = first_page_id_;
    int last_page_id = INVALID_PAGE_ID;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = pool_->FetchPage(current_page_id);

        if (page->GetFreeSpace() >= required_space) {
            pool_->UnpinPage(current_page_id, false);
            return current_page_id;
        }

        last_page_id = current_page_id;
        int next = page->GetNextPageId();
        pool_->UnpinPage(current_page_id, false);
        current_page_id = next;
    }

    // No page has enough space — allocate a new one and link it
    int new_page_id;
    Page* new_page = pool_->NewPage(new_page_id);
    new_page->Init(new_page_id);
    pool_->UnpinPage(new_page_id, true);

    // Link the last page to the new one
    if (last_page_id != INVALID_PAGE_ID) {
        Page* last_page = pool_->FetchPage(last_page_id);
        last_page->SetNextPageId(new_page_id);
        pool_->UnpinPage(last_page_id, true);
    }

    return new_page_id;
}
