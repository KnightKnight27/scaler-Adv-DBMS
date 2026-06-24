#include "storage/Page.hpp"

using namespace std;

namespace minidb {

Page::Page() { memset(data_, 0, PAGE_SIZE); }

void Page::init(uint32_t page_id, PageType type) {
    memset(data_, 0, PAGE_SIZE);
    memcpy(data_ + 0, &page_id, 4);
    uint32_t t = static_cast<uint32_t>(type);
    memcpy(data_ + 4, &t, 4);
    uint32_t count = 0;
    memcpy(data_ + 8, &count, 4);
    uint32_t free_offset = HEADER_SIZE + maxSlots() * SLOT_ENTRY_SIZE;
    memcpy(data_ + 12, &free_offset, 4);
}

uint32_t Page::getPageId() const {
    uint32_t id = 0;
    memcpy(&id, data_ + 0, 4);
    return id;
}

PageType Page::getPageType() const {
    uint32_t t = 0;
    memcpy(&t, data_ + 4, 4);
    return static_cast<PageType>(t);
}

uint32_t Page::getRecordCount() const {
    uint32_t c = 0;
    memcpy(&c, data_ + 8, 4);
    return c;
}

uint32_t Page::getFreeSpaceOffset() const {
    uint32_t o = 0;
    memcpy(&o, data_ + 12, 4);
    return o;
}

void Page::setRecordCount(uint32_t count) { memcpy(data_ + 8, &count, 4); }

void Page::setFreeSpaceOffset(uint32_t offset) { memcpy(data_ + 12, &offset, 4); }

const char* Page::getData() const { return data_; }

char* Page::getData() { return data_; }

uint32_t Page::slotDirectoryOffset() const { return HEADER_SIZE; }

uint32_t Page::maxSlots() const { return 128; }

bool Page::insertRecord(const vector<char>& bytes) {
    uint32_t count = getRecordCount();
    if (count >= maxSlots()) return false;

    uint32_t free_offset = getFreeSpaceOffset();
    uint32_t needed = 4 + static_cast<uint32_t>(bytes.size());
    if (free_offset + needed > PAGE_SIZE) return false;

    uint32_t len = static_cast<uint32_t>(bytes.size());
    memcpy(data_ + free_offset, &len, 4);
    memcpy(data_ + free_offset + 4, bytes.data(), bytes.size());

    uint32_t slot_pos = slotDirectoryOffset() + count * SLOT_ENTRY_SIZE;
    memcpy(data_ + slot_pos, &free_offset, 4);

    setFreeSpaceOffset(free_offset + needed);
    setRecordCount(count + 1);
    return true;
}

bool Page::getRecord(uint32_t slot_index, vector<char>& out) const {
    uint32_t count = getRecordCount();
    if (slot_index >= count) return false;

    uint32_t offset = 0;
    uint32_t slot_pos = slotDirectoryOffset() + slot_index * SLOT_ENTRY_SIZE;
    memcpy(&offset, data_ + slot_pos, 4);
    if (offset == 0) return false;

    uint32_t len = 0;
    memcpy(&len, data_ + offset, 4);
    out.resize(len);
    memcpy(out.data(), data_ + offset + 4, len);
    return true;
}

bool Page::deleteRecord(uint32_t slot_index) {
    uint32_t count = getRecordCount();
    if (slot_index >= count) return false;
    uint32_t slot_pos = slotDirectoryOffset() + slot_index * SLOT_ENTRY_SIZE;
    uint32_t zero = 0;
    memcpy(data_ + slot_pos, &zero, 4);
    return true;
}

bool Page::isSlotDeleted(uint32_t slot_index) const {
    uint32_t count = getRecordCount();
    if (slot_index >= count) return true;
    uint32_t offset = 0;
    uint32_t slot_pos = slotDirectoryOffset() + slot_index * SLOT_ENTRY_SIZE;
    memcpy(&offset, data_ + slot_pos, 4);
    return offset == 0;
}

}  // namespace minidb
