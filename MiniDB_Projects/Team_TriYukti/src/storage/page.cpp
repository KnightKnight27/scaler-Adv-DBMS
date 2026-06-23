#include "storage/page.h"

namespace minidb {

// 0: page_id_t (4 bytes)
// 4: lsn (4 bytes)
// 8: tuple_count (2 bytes)
// 10: free_space_pointer (2 bytes)
// 12: next_page_id (4 bytes)
// Size = 16 bytes
// Followed by slot array: each slot is {uint16_t offset, uint16_t length} (4 bytes)

constexpr int PAGE_HEADER_SIZE = 16;
constexpr int SLOT_SIZE = 4;

Page::Page() {
    memset(data_, 0, PAGE_SIZE);
}

void Page::Init(page_id_t page_id) {
    memset(data_, 0, PAGE_SIZE);
    SetPageId(page_id);
    SetLSN(0);
    SetTupleCount(0);
    SetFreeSpacePointer(PAGE_SIZE);
    SetNextPageId(INVALID_PAGE_ID);
}

page_id_t Page::GetPageId() const {
    page_id_t page_id;
    memcpy(&page_id, data_, sizeof(page_id_t));
    return page_id;
}

void Page::SetPageId(page_id_t page_id) {
    memcpy(data_, &page_id, sizeof(page_id_t));
}

page_id_t Page::GetNextPageId() const {
    page_id_t next_page_id;
    memcpy(&next_page_id, data_ + 12, sizeof(page_id_t));
    return next_page_id;
}

void Page::SetNextPageId(page_id_t next_page_id) {
    memcpy(data_ + 12, &next_page_id, sizeof(page_id_t));
}

int32_t Page::GetLSN() const {
    int32_t lsn;
    memcpy(&lsn, data_ + 4, sizeof(int32_t));
    return lsn;
}

void Page::SetLSN(int32_t lsn) {
    memcpy(data_ + 4, &lsn, sizeof(int32_t));
}

uint16_t Page::GetTupleCount() const {
    uint16_t count;
    memcpy(&count, data_ + 8, sizeof(uint16_t));
    return count;
}

void Page::SetTupleCount(uint16_t tuple_count) {
    memcpy(data_ + 8, &tuple_count, sizeof(uint16_t));
}

uint16_t Page::GetFreeSpacePointer() const {
    uint16_t ptr;
    memcpy(&ptr, data_ + 10, sizeof(uint16_t));
    return ptr;
}

void Page::SetFreeSpacePointer(uint16_t free_space_pointer) {
    memcpy(data_ + 10, &free_space_pointer, sizeof(uint16_t));
}

uint16_t Page::GetTupleOffset(slot_id_t slot_id) const {
    uint16_t offset;
    memcpy(&offset, data_ + PAGE_HEADER_SIZE + slot_id * SLOT_SIZE, sizeof(uint16_t));
    return offset;
}

void Page::SetTupleOffset(slot_id_t slot_id, uint16_t offset) {
    memcpy(data_ + PAGE_HEADER_SIZE + slot_id * SLOT_SIZE, &offset, sizeof(uint16_t));
}

uint16_t Page::GetTupleLength(slot_id_t slot_id) const {
    uint16_t length;
    memcpy(&length, data_ + PAGE_HEADER_SIZE + slot_id * SLOT_SIZE + 2, sizeof(uint16_t));
    return length;
}

void Page::SetTupleLength(slot_id_t slot_id, uint16_t length) {
    memcpy(data_ + PAGE_HEADER_SIZE + slot_id * SLOT_SIZE + 2, &length, sizeof(uint16_t));
}

uint16_t Page::GetFreeSpaceRemaining() const {
    return GetFreeSpacePointer() - PAGE_HEADER_SIZE - GetTupleCount() * SLOT_SIZE;
}

bool Page::InsertTuple(const Tuple &tuple, RecordId *rid) {
    if (tuple.data_.empty()) return false;
    uint16_t tuple_size = tuple.data_.size();

    uint16_t count = GetTupleCount();
    for (slot_id_t i = 0; i < count; ++i) {
        if (GetTupleLength(i) == 0) { 
            if (GetFreeSpaceRemaining() >= tuple_size) {
                uint16_t new_offset = GetFreeSpacePointer() - tuple_size;
                SetFreeSpacePointer(new_offset);
                SetTupleOffset(i, new_offset);
                SetTupleLength(i, tuple_size);
                memcpy(data_ + new_offset, tuple.data_.data(), tuple_size);
                rid->page_id = GetPageId();
                rid->slot_id = i;
                return true;
            }
        }
    }

    if (GetFreeSpaceRemaining() < tuple_size + SLOT_SIZE) {
        return false;
    }

    uint16_t new_offset = GetFreeSpacePointer() - tuple_size;
    SetFreeSpacePointer(new_offset);
    
    slot_id_t new_slot_id = count;
    SetTupleOffset(new_slot_id, new_offset);
    SetTupleLength(new_slot_id, tuple_size);
    SetTupleCount(count + 1);
    
    memcpy(data_ + new_offset, tuple.data_.data(), tuple_size);
    
    rid->page_id = GetPageId();
    rid->slot_id = new_slot_id;
    return true;
}

bool Page::DeleteTuple(slot_id_t slot_id) {
    if (slot_id >= GetTupleCount()) return false;
    if (GetTupleLength(slot_id) == 0) return false;
    SetTupleLength(slot_id, 0);
    return true;
}

bool Page::UpdateTuple(slot_id_t slot_id, const Tuple &tuple) {
    if (slot_id >= GetTupleCount()) return false;
    uint16_t length = GetTupleLength(slot_id);
    if (length == 0 || length != tuple.data_.size()) return false;
    uint16_t offset = GetTupleOffset(slot_id);
    memcpy(data_ + offset, tuple.data_.data(), length);
    return true;
}

bool Page::GetTuple(slot_id_t slot_id, Tuple *tuple) const {
    if (slot_id >= GetTupleCount()) return false;
    uint16_t length = GetTupleLength(slot_id);
    if (length == 0) return false;

    uint16_t offset = GetTupleOffset(slot_id);
    tuple->data_.resize(length);
    memcpy(tuple->data_.data(), data_ + offset, length);
    tuple->rid_.page_id = GetPageId();
    tuple->rid_.slot_id = slot_id;
    return true;
}

char* Page::GetData() {
    return data_;
}

const char* Page::GetData() const {
    return data_;
}

} // namespace minidb
