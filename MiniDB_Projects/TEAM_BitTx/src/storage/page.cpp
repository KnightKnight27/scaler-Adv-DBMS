#include "storage/page.h"

#include "common/rid.h"

#include <cstring>

namespace minidb {

namespace {

constexpr int32_t kPageHeaderSize = static_cast<int32_t>(sizeof(PageHeader));

}

SlotEntry* Page::GetSlot(int32_t slotNum) {
  int32_t slotOffset = kPageHeaderSize + slotNum * static_cast<int32_t>(sizeof(SlotEntry));
  return reinterpret_cast<SlotEntry*>(data_ + slotOffset);
}

const SlotEntry* Page::GetSlot(int32_t slotNum) const {
  int32_t slotOffset = kPageHeaderSize + slotNum * static_cast<int32_t>(sizeof(SlotEntry));
  return reinterpret_cast<const SlotEntry*>(data_ + slotOffset);
}

void Page::EnsureSlotArraySize(int32_t needed) {
  if (needed > header_.numSlots) {
    header_.numSlots = needed;
  }
  int32_t required = needed * static_cast<int32_t>(sizeof(SlotEntry)) + kPageHeaderSize;
  if (required > header_.freeSpaceOffset) {
    header_.freeSpaceOffset = required;
    header_.freeSpaceSize = PAGE_SIZE - required;
  }
  slotArrayCapacity_ = needed;
}

void Page::WriteHeader() const {
  memcpy(const_cast<char*>(data_), &header_, sizeof(PageHeader));
}

void Page::ReadHeader() {
  memcpy(&header_, data_, sizeof(PageHeader));
}

int32_t Page::InsertTuple(const char* tupleData, int32_t tupleSize) {
  int32_t newSlot = header_.numSlots;
  EnsureSlotArraySize(newSlot + 1);

  int32_t totalSpace = tupleSize + static_cast<int32_t>(sizeof(SlotEntry));
  if (totalSpace > header_.freeSpaceSize) {
    return -1;
  }

  int32_t tupleOffset = header_.tupleHigh - tupleSize;
  if (tupleOffset < header_.freeSpaceOffset) {
    return -1;
  }
  memcpy(data_ + tupleOffset, tupleData, tupleSize);
  header_.tupleHigh = tupleOffset;
  header_.freeSpaceSize -= tupleSize;
  header_.freeSpaceOffset += static_cast<int32_t>(sizeof(SlotEntry));

  SlotEntry* slot = GetSlot(newSlot);
  slot->offset = tupleOffset;
  slot->size = tupleSize;

  WriteHeader();
  return newSlot;
}

bool Page::DeleteTuple(int32_t slotNum) {
  if (slotNum < 0 || slotNum >= header_.numSlots)
    return false;
  SlotEntry* slot = GetSlot(slotNum);
  if (!slot->IsValid())
    return false;

  slot->offset = -1;
  slot->size = 0;
  return true;
}

bool Page::UpdateTuple(int32_t slotNum, const char* tupleData, int32_t tupleSize) {
  if (slotNum < 0 || slotNum >= header_.numSlots)
    return false;
  SlotEntry* slot = GetSlot(slotNum);
  if (!slot->IsValid())
    return false;

  if (tupleSize <= slot->size) {
    memcpy(data_ + slot->offset, tupleData, tupleSize);
    slot->size = tupleSize;
    WriteHeader();
    return true;
  }

  int32_t grow = tupleSize - slot->size;
  if (header_.freeSpaceSize < grow) {
    return false;
  }
  int32_t newOffset = header_.tupleHigh - tupleSize;
  memcpy(data_ + newOffset, tupleData, tupleSize);
  header_.tupleHigh = newOffset;
  header_.freeSpaceSize -= grow;
  slot->offset = newOffset;
  slot->size = tupleSize;
  WriteHeader();
  return true;
}

bool Page::GetTuple(int32_t slotNum, const char*& data, int32_t& size) const {
  if (slotNum < 0 || slotNum >= header_.numSlots)
    return false;
  const SlotEntry* slot = GetSlot(slotNum);
  if (!slot->IsValid())
    return false;
  data = data_ + slot->offset;
  size = slot->size;
  return true;
}

} // namespace minidb