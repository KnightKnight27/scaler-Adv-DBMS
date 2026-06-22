#ifndef RID_H
#define RID_H

#include "config.h"
#include <string>

namespace minidb {

class RID {
public:
    RID() : page_id_(INVALID_PAGE_ID), slot_num_(0) {}
    RID(page_id_t page_id, uint32_t slot_num) : page_id_(page_id), slot_num_(slot_num) {}

    inline page_id_t GetPageId() const { return page_id_; }
    inline uint32_t GetSlotNum() const { return slot_num_; }

    inline void Set(page_id_t page_id, uint32_t slot_num) {
        page_id_ = page_id;
        slot_num_ = slot_num;
    }

    inline bool operator==(const RID& other) const {
        return page_id_ == other.page_id_ && slot_num_ == other.slot_num_;
    }

    inline std::string ToString() const {
        return "[" + std::to_string(page_id_) + ", " + std::to_string(slot_num_) + "]";
    }

private:
    page_id_t page_id_;
    uint32_t slot_num_;
};

} // namespace minidb

#endif // RID_H
