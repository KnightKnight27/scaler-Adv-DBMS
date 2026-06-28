#pragma once

#include "config.h"
#include <cstdint>
#include <string>

namespace minidb {

using page_id_t = int32_t;
using frame_id_t = int32_t;
using lsn_t = int64_t;
using txn_id_t = int32_t;
using slot_id_t = int32_t;

struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    slot_id_t slot_id{-1};

    bool operator==(const RID &other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
    bool operator!=(const RID &other) const { return !(*this == other); }
    bool operator<(const RID &other) const {
        if (page_id < other.page_id) return true;
        if (page_id > other.page_id) return false;
        return slot_id < other.slot_id;
    }
    bool IsValid() const { return page_id != INVALID_PAGE_ID && slot_id != -1; }
    std::string ToString() const {
        return "[" + std::to_string(page_id) + ", " + std::to_string(slot_id) + "]";
    }
};

} // namespace minidb

namespace std {
template <>
struct hash<minidb::RID> {
    size_t operator()(const minidb::RID &rid) const {
        return (static_cast<size_t>(rid.page_id) << 32) ^ static_cast<size_t>(rid.slot_id);
    }
};
} // namespace std
