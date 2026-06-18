#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "common/config.h"

namespace minidb {

// A Record ID uniquely locates a tuple on disk: (page id, slot number).
// Every index entry and lock is keyed by an RID, so it is the physical
// address of a row inside a heap file.
class RID {
 public:
  RID() = default;
  RID(page_id_t page_id, slot_id_t slot_num) : page_id_(page_id), slot_num_(slot_num) {}

  page_id_t GetPageId() const { return page_id_; }
  slot_id_t GetSlotNum() const { return slot_num_; }

  bool operator==(const RID &o) const { return page_id_ == o.page_id_ && slot_num_ == o.slot_num_; }
  bool operator!=(const RID &o) const { return !(*this == o); }
  bool operator<(const RID &o) const {
    return page_id_ != o.page_id_ ? page_id_ < o.page_id_ : slot_num_ < o.slot_num_;
  }

  // 64-bit packed form, handy as a map/lock key.
  int64_t Get() const { return (static_cast<int64_t>(page_id_) << 32) | slot_num_; }

  std::string ToString() const {
    return "(" + std::to_string(page_id_) + "," + std::to_string(slot_num_) + ")";
  }

 private:
  page_id_t page_id_{INVALID_PAGE_ID};
  slot_id_t slot_num_{0};
};

}  // namespace minidb

namespace std {
template <>
struct hash<minidb::RID> {
  size_t operator()(const minidb::RID &rid) const { return std::hash<int64_t>()(rid.Get()); }
};
}  // namespace std
