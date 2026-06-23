#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace minidb {

// One versioned value in the LSM. A delete is a tombstone (we never modify data
// in place — newest write wins, resolved at read/compaction time).
struct LsmEntry {
    std::string   row;
    bool          tombstone = false;
    std::uint64_t seq       = 0;
};

// The in-memory write buffer: an ordered map so it can be flushed to a sorted
// SSTable directly and scanned in key order. A std::map keeps the code simple
// (a production LSM uses a concurrent skiplist).
class MemTable {
public:
    void put(std::int64_t key, std::string row, std::uint64_t seq) {
        LsmEntry& e = map_[key];
        bytes_ += row.size() >= e.row.size() ? (row.size() - e.row.size())
                                             : 0;  // approximate size tracking
        e = LsmEntry{std::move(row), false, seq};
    }
    void del(std::int64_t key, std::uint64_t seq) {
        map_[key] = LsmEntry{"", true, seq};
    }
    bool get(std::int64_t key, LsmEntry& out) const {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        out = it->second;
        return true;
    }

    const std::map<std::int64_t, LsmEntry>& entries() const { return map_; }
    std::size_t bytes() const { return bytes_; }
    bool empty() const { return map_.empty(); }
    void clear() { map_.clear(); bytes_ = 0; }

private:
    std::map<std::int64_t, LsmEntry> map_;
    std::size_t                      bytes_ = 0;
};

} // namespace minidb
