// MiniDB - Record Identifier. A row is located by (page_id, slot_num).
// RIDs are stable: deleting a tuple tombstones its slot rather than shifting others,
// so an index entry pointing at a RID never silently moves to a different row.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace minidb {

struct RID {
    int32_t page_id = -1;
    int32_t slot_num = -1;

    RID() = default;
    RID(int32_t p, int32_t s) : page_id(p), slot_num(s) {}

    bool IsValid() const { return page_id >= 0 && slot_num >= 0; }
    bool operator==(const RID& o) const { return page_id == o.page_id && slot_num == o.slot_num; }
    bool operator!=(const RID& o) const { return !(*this == o); }
    bool operator<(const RID& o) const {
        return page_id != o.page_id ? page_id < o.page_id : slot_num < o.slot_num;
    }

    // A single integer handle for the lock manager and wait-for graph.
    int64_t AsKey() const { return (static_cast<int64_t>(page_id) << 20) | (slot_num & 0xFFFFF); }
    std::string ToString() const {
        return "(" + std::to_string(page_id) + "," + std::to_string(slot_num) + ")";
    }
};

}  // namespace minidb

namespace std {
template <>
struct hash<minidb::RID> {
    size_t operator()(const minidb::RID& r) const noexcept {
        return std::hash<int64_t>()(r.AsKey());
    }
};
}  // namespace std
