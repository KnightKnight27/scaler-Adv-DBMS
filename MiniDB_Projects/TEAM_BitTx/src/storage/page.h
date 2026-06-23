#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace minidb {

using namespace std;

constexpr int32_t PAGE_SIZE = 4096;
constexpr int32_t SLOT_ARRAY_GROWTH = 16;

struct PageHeader {
  int32_t numSlots;
  int32_t freeSpaceOffset;
  int32_t freeSpaceSize;
  int32_t nextFreePage;
  int32_t tupleHigh;

  void Init() {
    numSlots = 0;
    freeSpaceOffset = sizeof(PageHeader);
    freeSpaceSize = PAGE_SIZE - sizeof(PageHeader);
    nextFreePage = -1;
    tupleHigh = PAGE_SIZE;
  }
};

struct SlotEntry {
  int32_t offset;
  int32_t size;

  bool IsValid() const {
    return offset >= 0 && size > 0;
  }
};

class Page {
public:
  Page() = default;
  explicit Page(int32_t pageId) : pageId_(pageId) {}

  void Init(int32_t pageId) {
    pageId_ = pageId;
    header_.Init();
    memset(data_, 0, PAGE_SIZE);
  }

  void Reset(int32_t pageId) {
    pageId_ = pageId;
  }

  int32_t GetPageId() const {
    return pageId_;
  }
  PageHeader& GetHeader() {
    return header_;
  }
  const PageHeader& GetHeader() const {
    return header_;
  }

  char* GetData() {
    return data_;
  }
  const char* GetData() const {
    return data_;
  }

  int32_t GetNumSlots() const {
    return header_.numSlots;
  }
  int32_t GetFreeSpace() const {
    return header_.freeSpaceSize;
  }
  int32_t GetFreeSpaceOffset() const {
    return header_.freeSpaceOffset;
  }
  int32_t GetNextFreePage() const {
    return header_.nextFreePage;
  }
  void SetNextFreePage(int32_t p) {
    header_.nextFreePage = p;
  }

  SlotEntry* GetSlot(int32_t slotNum);
  const SlotEntry* GetSlot(int32_t slotNum) const;

  int32_t InsertTuple(const char* tupleData, int32_t tupleSize);
  bool DeleteTuple(int32_t slotNum);
  bool UpdateTuple(int32_t slotNum, const char* tupleData, int32_t tupleSize);

  bool GetTuple(int32_t slotNum, const char*& data, int32_t& size) const;

  string ToString() const;

  void WriteHeader() const;
  void ReadHeader();
  void EnsureSlotArraySize(int32_t needed);

private:
  int32_t pageId_ = -1;
  PageHeader header_;
  int32_t slotArrayCapacity_ = 0;
  char data_[PAGE_SIZE];
};

} // namespace minidb