#ifndef RID_H
#define RID_H

#include "config.h"
#include <string>

namespace minidb {

/**
 * Record Identifier (RID) representing the physical location of a tuple on disk
 * via its page ID and slot number inside that page.
 */
class RID {
public:
    // Default constructor creating an invalid record identifier
    RID() : page_id_(INVALID_PAGE_ID), slot_num_(0) {}
    
    // Parameterized constructor specifying page location and slot index
    RID(page_id_t page_id, uint32_t slot_num) : page_id_(page_id), slot_num_(slot_num) {}

    // Gets the associated page ID
    inline page_id_t GetPageId() const { return page_id_; }
    
    // Gets the slot offset index inside the page
    inline uint32_t GetSlotNum() const { return slot_num_; }

    // Re-sets the identifier values
    inline void Set(page_id_t page_id, uint32_t slot_num) {
        page_id_ = page_id;
        slot_num_ = slot_num;
    }

    // Equality comparison operator
    inline bool operator==(const RID& other) const {
        return page_id_ == other.page_id_ && slot_num_ == other.slot_num_;
    }

    // Formats identifier to string format [page_id, slot_num]
    inline std::string ToString() const {
        return "[" + std::to_string(page_id_) + ", " + std::to_string(slot_num_) + "]";
    }

private:
    page_id_t page_id_;
    uint32_t slot_num_;
};

} // namespace minidb

#endif // RID_H
