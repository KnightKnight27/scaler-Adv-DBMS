#pragma once

#include <cstdint>
#include <string>

namespace minidb {

using namespace std;

class RecordId {
public:
  RecordId() = default;
  RecordId(int32_t pageId, int32_t slotNum) : pageId_(pageId), slotNum_(slotNum) {}

  int32_t GetPageId() const {
    return pageId_;
  }
  int32_t GetSlotNum() const {
    return slotNum_;
  }

  void SetPageId(int32_t pid) {
    pageId_ = pid;
  }
  void SetSlotNum(int32_t sn) {
    slotNum_ = sn;
  }

  bool IsValid() const {
    return pageId_ >= 0 && slotNum_ >= 0;
  }

  bool operator==(const RecordId& other) const {
    return pageId_ == other.pageId_ && slotNum_ == other.slotNum_;
  }
  bool operator!=(const RecordId& other) const {
    return !(*this == other);
  }
  bool operator<(const RecordId& other) const {
    if (pageId_ != other.pageId_)
      return pageId_ < other.pageId_;
    return slotNum_ < other.slotNum_;
  }

  string ToString() const;

private:
  int32_t pageId_ = -1;
  int32_t slotNum_ = -1;
};

constexpr int32_t INVALID_PAGE_ID = -1;
constexpr int32_t INVALID_SLOT_NUM = -1;

} // namespace minidb
