#include "storage/page.h"
#include <cstring>
#include <stdexcept>

// ============================================================
// Header layout offsets (within data_[])
//   [0..3]   page_id       (int32)
//   [4..5]   num_slots     (uint16)
//   [6..7]   free_space_ptr(uint16)  — where free region begins
//   [8..11]  next_page_id  (int32)
//   [12..13] flags         (uint16)
//   [14..15] padding
// ============================================================

static constexpr int OFF_PAGE_ID   = 0;
static constexpr int OFF_NUM_SLOTS = 4;
static constexpr int OFF_FREE_PTR  = 6;
static constexpr int OFF_NEXT_PAGE = 8;
static constexpr int OFF_FLAGS     = 12;

// ============================================================
// Init — zero out the page and set up an empty slotted page
// ============================================================

void Page::Init(int page_id) {
    memset(data_, 0, PAGE_SIZE);
    SetPageId(page_id);
    SetNumSlots(0);
    SetFreeSpacePtr(PAGE_SIZE);  // free space starts at end (row data grows backward)
    SetNextPageId(INVALID_PAGE_ID);
}

// ============================================================
// Row operations
// ============================================================

int Page::InsertRow(const char* row_data, int row_size) {
    // Need space for the row data + one new slot entry (4 bytes)
    int available = GetFreeSpaceAmount();
    if (row_size + SLOT_SIZE > available) {
        return -1;  // no room
    }

    // Grow row data backward from the current free_space_ptr
    uint16_t new_ptr = GetFreeSpacePtr() - row_size;
    memcpy(data_ + new_ptr, row_data, row_size);
    SetFreeSpacePtr(new_ptr);

    // Add a new slot entry pointing to this row
    int slot_id = GetNumSlots();
    SetNumSlots(slot_id + 1);
    SetSlotEntry(slot_id, new_ptr, row_size);

    return slot_id;
}

bool Page::GetRow(int slot_id, char* out_data, int* out_size) const {
    if (slot_id < 0 || slot_id >= GetNumSlots()) return false;

    uint16_t offset = GetSlotOffset(slot_id);
    uint16_t length = GetSlotLength(slot_id);

    // Deleted slot
    if (offset == 0 && length == 0) return false;

    memcpy(out_data, data_ + offset, length);
    *out_size = length;
    return true;
}

bool Page::DeleteRow(int slot_id) {
    if (slot_id < 0 || slot_id >= GetNumSlots()) return false;

    uint16_t offset = GetSlotOffset(slot_id);
    uint16_t length = GetSlotLength(slot_id);
    if (offset == 0 && length == 0) return false;  // already deleted

    // Mark as tombstone — we don't reclaim space (simple approach)
    // A real DB would compact the page, but this is fine for our scope
    SetSlotEntry(slot_id, 0, 0);
    return true;
}

bool Page::UpdateRow(int slot_id, const char* new_data, int new_size) {
    if (slot_id < 0 || slot_id >= GetNumSlots()) return false;

    uint16_t old_offset = GetSlotOffset(slot_id);
    uint16_t old_length = GetSlotLength(slot_id);
    if (old_offset == 0 && old_length == 0) return false;  // deleted

    // If new data fits in the old space, update in-place
    if (new_size <= old_length) {
        memcpy(data_ + old_offset, new_data, new_size);
        // Update length (offset stays the same, wastes a bit of space)
        SetSlotEntry(slot_id, old_offset, new_size);
        return true;
    }

    // Doesn't fit — caller must handle (delete here + insert elsewhere + forwarding)
    return false;
}

// ============================================================
// Header accessors
// ============================================================

int Page::GetPageId() const {
    int id;
    memcpy(&id, data_ + OFF_PAGE_ID, sizeof(int));
    return id;
}

void Page::SetPageId(int id) {
    memcpy(data_ + OFF_PAGE_ID, &id, sizeof(int));
}

int Page::GetNumSlots() const {
    uint16_t n;
    memcpy(&n, data_ + OFF_NUM_SLOTS, sizeof(uint16_t));
    return n;
}

void Page::SetNumSlots(uint16_t n) {
    memcpy(data_ + OFF_NUM_SLOTS, &n, sizeof(uint16_t));
}

uint16_t Page::GetFreeSpacePtr() const {
    uint16_t ptr;
    memcpy(&ptr, data_ + OFF_FREE_PTR, sizeof(uint16_t));
    return ptr;
}

void Page::SetFreeSpacePtr(uint16_t ptr) {
    memcpy(data_ + OFF_FREE_PTR, &ptr, sizeof(uint16_t));
}

int Page::GetNextPageId() const {
    int id;
    memcpy(&id, data_ + OFF_NEXT_PAGE, sizeof(int));
    return id;
}

void Page::SetNextPageId(int next) {
    memcpy(data_ + OFF_NEXT_PAGE, &next, sizeof(int));
}

int Page::GetFreeSpace() const {
    return GetFreeSpaceAmount();
}

// ============================================================
// Slot directory helpers
//
// Slot directory starts right after the header (byte 16).
// Each slot entry = [offset(2B), length(2B)] = 4 bytes.
// Slot i starts at byte: PAGE_HEADER_SIZE + i * SLOT_SIZE
// ============================================================

uint16_t Page::GetSlotOffset(int slot_id) const {
    int pos = PAGE_HEADER_SIZE + slot_id * SLOT_SIZE;
    uint16_t offset;
    memcpy(&offset, data_ + pos, sizeof(uint16_t));
    return offset;
}

uint16_t Page::GetSlotLength(int slot_id) const {
    int pos = PAGE_HEADER_SIZE + slot_id * SLOT_SIZE + sizeof(uint16_t);
    uint16_t length;
    memcpy(&length, data_ + pos, sizeof(uint16_t));
    return length;
}

void Page::SetSlotEntry(int slot_id, uint16_t offset, uint16_t length) {
    int pos = PAGE_HEADER_SIZE + slot_id * SLOT_SIZE;
    memcpy(data_ + pos, &offset, sizeof(uint16_t));
    memcpy(data_ + pos + sizeof(uint16_t), &length, sizeof(uint16_t));
}

// Free space = gap between end of slot directory and start of row data
int Page::GetFreeSpaceAmount() const {
    int slot_dir_end = PAGE_HEADER_SIZE + GetNumSlots() * SLOT_SIZE;
    int row_data_start = GetFreeSpacePtr();
    return row_data_start - slot_dir_end;
}
